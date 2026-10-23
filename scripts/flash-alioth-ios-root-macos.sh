#!/usr/bin/env bash
# Preflight and optionally flash the alioth iOS root on the macOS host.
#
# Usage:
#   scripts/flash-alioth-ios-root-macos.sh preflight ROOT.simg BOOT.img
#   RUN=1 scripts/flash-alioth-ios-root-macos.sh flash ROOT.simg BOOT.img
#   scripts/flash-alioth-ios-root-macos.sh boot BOOT.img
#
# The flash path writes only the shared userdata partition, never boot_a,
# metadata, super, or the GPT. fastboot boot is non-persistent.
set -euo pipefail

MODE=${1:-preflight}
ROOT=${2:-}
ROOT_IMAGE=${ROOT:-"$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/out/alioth/ios/boot/alioth-ios16-diag.img"}
BOOT_IMAGE=${3:-"$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/out/alioth/ios/boot/alioth-ios16-diag.img"}
SERIAL=${ALIOTH_SERIAL:-9c6a8eb6}
FLASH_CHUNK=${FLASH_CHUNK:-512M}

case "$MODE" in
  preflight|flash|boot) ;;
  *) echo "unsupported mode: $MODE" >&2; exit 2 ;;
esac

if [[ "$MODE" == boot ]]; then
  BOOT_IMAGE=${2:-"$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/out/alioth/ios/boot/alioth-ios16-diag.img"}
  [[ -f "$BOOT_IMAGE" ]] || { echo "missing artifact: $BOOT_IMAGE" >&2; exit 2; }
  ROOT_IMAGE=
else
  [[ -f "$ROOT_IMAGE" && -f "$BOOT_IMAGE" ]] || {
    echo "missing ROOT.simg or BOOT.img" >&2
    exit 2
  }
fi
for tool in fastboot python3.13 shasum xxd; do
  command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done

if [[ "$MODE" != boot ]]; then
  sparse_magic=$(xxd -p -l 4 "$ROOT_IMAGE" | tr -d '\n')
  [[ "$sparse_magic" == 3aff26ed ]] || {
    echo "root image is not Android sparse (magic=$sparse_magic)" >&2
    exit 2
  }
fi

devices=$(fastboot devices -l)
printf '%s\n' "$devices"
printf '%s\n' "$devices" | grep -q "^$SERIAL[[:space:]]" || {
  echo "serial $SERIAL is not in fastboot" >&2
  exit 2
}

getvar() {
  local name=$1
  python3 - "$SERIAL" "$name" <<'PY'
import subprocess, sys

serial, name = sys.argv[1:3]
try:
    result = subprocess.run(
        ['fastboot', '-s', serial, 'getvar', name],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=10,
    )
except subprocess.TimeoutExpired as error:
    output = error.stdout or ''
    if isinstance(output, bytes):
        output = output.decode(errors='replace')
    print(output, end='')
    print(f'fastboot getvar {name}: timed out', file=sys.stderr)
    sys.exit(124)
print(result.stdout, end='')
sys.exit(result.returncode)
PY
}

product=$(getvar product)
printf '%s\n' "$product" | grep -Eq '^product:[[:space:]]*alioth$' || {
  echo "unexpected fastboot product" >&2
  exit 2
}
boot_bytes=$(stat -f '%z' "$BOOT_IMAGE")

if [[ "$MODE" != boot ]]; then
  slot=$(getvar current-slot)
  unlocked=$(getvar unlocked)
  partition=$(getvar partition-size:userdata)

  printf '%s\n' "$slot" "$unlocked" "$partition"
  printf '%s\n' "$slot" | grep -Eq '^current-slot:[[:space:]]*a$' || {
    echo "current-slot is not a" >&2
    exit 2
  }
  printf '%s\n' "$unlocked" | grep -Eq '^unlocked:[[:space:]]*yes$' || {
    echo "bootloader is not unlocked" >&2
    exit 2
  }

  partition_hex=$(printf '%s\n' "$partition" |
    sed -nE 's/^partition-size:userdata:[[:space:]]*(0x[0-9a-fA-F]+).*$/\1/p')
  [[ -n "$partition_hex" ]] || {
    echo "cannot parse userdata partition size" >&2
    exit 2
  }
  partition_bytes=$((partition_hex))
  root_bytes=$(stat -f '%z' "$ROOT_IMAGE")
  sparse_info=$(python3.13 \
    "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android-sparse-info.py" \
    "$ROOT_IMAGE")
  sparse_bytes=$(printf '%s\n' "$sparse_info" |
    sed -nE 's/^bytes=([0-9]+)$/\1/p')
  [[ -n "$sparse_bytes" ]] || {
    echo "cannot read sparse logical capacity" >&2
    exit 2
  }
  (( partition_bytes > sparse_bytes )) || {
    echo "userdata ($partition_bytes) is not larger than sparse root ($sparse_bytes)" >&2
    exit 2
  }

  echo "=== artifact hashes ==="
  shasum -a 256 "$ROOT_IMAGE" "$BOOT_IMAGE"
  printf '%s\n' "$sparse_info"
  printf 'userdata bytes: %s\nroot archive bytes: %s\nroot logical bytes: %s\nboot image bytes: %s\n' \
    "$partition_bytes" "$root_bytes" "$sparse_bytes" "$boot_bytes"
fi

if [[ "$MODE" == preflight ]]; then
  echo "ALIOTH_IOS_PREFLIGHT_OK"
  exit 0
fi

if [[ "$MODE" == flash ]]; then
  [[ ${RUN:-0} == 1 ]] || {
    echo "set RUN=1 to authorize the userdata write and diagnostic boot" >&2
    exit 2
  }

  echo "=== flashing GPT userdata by partition name ==="
  fastboot -s "$SERIAL" -S "$FLASH_CHUNK" flash userdata "$ROOT_IMAGE"

  echo "=== re-reading device state ==="
  if ! slot=$(getvar current-slot); then
    echo "warning: current-slot readback timed out; the write completed" >&2
  elif ! printf '%s\n' "$slot" | grep -Eq '^current-slot:[[:space:]]*a$'; then
    echo "warning: unexpected post-flash slot: $slot" >&2
  fi
  if ! partition=$(getvar partition-size:userdata); then
    echo "warning: userdata size readback timed out; the write completed" >&2
  elif ! printf '%s\n' "$partition" | grep -Eq '^partition-size:userdata:[[:space:]]*0x385[Bb]7[Ff][Bb]000'; then
    echo "warning: unexpected post-flash userdata size: $partition" >&2
  fi
fi

echo "=== non-persistent diagnostic boot ==="
fastboot -s "$SERIAL" boot "$BOOT_IMAGE"
echo "ALIOTH_IOS_DIAG_BOOT_SENT"
