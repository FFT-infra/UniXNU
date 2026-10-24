#!/usr/bin/env bash
# Build the alioth iPhone X iOS 16.7.16 root image using native macOS APFS
# reads and libguestfs tar-in writes. The Linux hfsplus driver cannot safely
# read every LZBITMAP file from this IPSW, so apfs-fuse is deliberately unused.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:-"$ROOT/out/alioth/userdata/ios16-root-20H392-v15-staging.img"}
SIZE=${2:-16G}
OS_VOLUME=${OS_VOLUME:-/Volumes/SydneyUpdate20H392.D22D221OS}
SYS_VOLUME=${SYS_VOLUME:-/Volumes/SydneyUpdate20H392.D22D221SystemCryptex}
APP_VOLUME=${APP_VOLUME:-/Volumes/AppCryptex}
SECURE_OVERLAY=${SECURE_OVERLAY:-}
IOS_SWEET=${IOS_SWEET:-/Users/xinyang/Lab/Bridge/WorkSpace/alioth/ios-sweet}
REMOTE=${REMOTE:-orb}
POST="$ROOT/scripts/alioth-ios-root-post.py"
WALLPAPER="$ROOT/scripts/make-wallpaper-overlay-alioth.py"
VERIFY="$ROOT/scripts/verify-alioth-ios-root.py"

case "$SIZE" in
  16G|20G|24G|32G) ;;
  *) echo "unsupported image size: $SIZE" >&2; exit 2 ;;
esac
for path in "$OS_VOLUME/System/Library/xpc/launchd.plist" \
            "$SYS_VOLUME/System/Library" "$APP_VOLUME/System/Library" \
            "$POST" "$VERIFY" "$WALLPAPER"; do
  [[ -e "$path" ]] || { echo "missing input: $path" >&2; exit 2; }
done
[[ -z "$SECURE_OVERLAY" || -d "$SECURE_OVERLAY" ]] || {
  echo "missing secure overlay: $SECURE_OVERLAY" >&2
  exit 2
}
for tool in tar ssh; do
  command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done
mkdir -p "$(dirname "$OUT")"

shared_remote_path() {
  local value=$1
  case "$value" in
    /Users/xinyang/Lab/Bridge/*)
      printf '/home/xinyang/Lab/Bridge/%s\n' \
        "${value#/Users/xinyang/Lab/Bridge/}"
      ;;
    /Volumes/Disk/xinyang/Lab/Bridge/*|/Volumes/disk/xinyang/Lab/Bridge/*)
      printf '/home/xinyang/Lab/Bridge/%s\n' \
        "${value#/Volumes/*/xinyang/Lab/Bridge/}"
      ;;
    *)
      echo "path must be below the shared Bridge volume: $value" >&2
      return 1
      ;;
  esac
}

if [[ -e "$OUT" && ${ALLOW_REBUILD:-0} != 1 ]]; then
  echo "refusing to overwrite $OUT (set ALLOW_REBUILD=1)" >&2
  exit 2
fi
REMOTE_OUT=$(shared_remote_path "$OUT")
REMOTE_POST=$(shared_remote_path "$POST")
REMOTE_VERIFY=$(shared_remote_path "$VERIFY")
REMOTE_WALLPAPER=$(shared_remote_path "$WALLPAPER")
REMOTE_MOUNT="/tmp/alioth-ios-root-$$"

remote() {
  ssh "$REMOTE" "$@"
}

stream_tree() {
  local source=$1
  local destination=$2
  echo "=== streaming $source -> $destination ==="
  tar -cpf - --disable-copyfile --no-acls --no-xattrs \
    --uname 0 --gname 0 \
    --exclude='./.ba' --exclude='./.mb' --exclude='./.file' \
    --exclude='./private/var/audit' \
    --exclude='./private/var/root' \
    --exclude='./private/var/db/accessoryupdater' \
    --exclude='./private/var/db/analyticsd' \
    --exclude='./private/var/db/com.apple.xpc.roleaccountd.staging' \
    --exclude='./private/var/db/coreml' \
    --exclude='./private/var/db/darwindaemon' \
    --exclude='./private/var/db/datadetectors' \
    --exclude='./private/var/db/diagnostics' \
    --exclude='./private/var/db/diskimagesiod' \
    --exclude='./private/var/db/gpsd' \
    --exclude='./private/var/db/knowledgegraphd' \
    --exclude='./private/var/db/lockdown' \
    --exclude='./private/var/db/mmaintenanced' \
    --exclude='./private/var/db/reportmemoryexception' \
    --exclude='./private/var/db/rmd' \
    --exclude='./private/var/db/Sandbox' \
    --exclude='./private/var/db/sysdiagnose/com.apple.sysdiagnose' \
    --exclude='./private/var/db/timed' \
    --exclude='./private/etc/master.passwd' \
    --exclude='./private/etc/racoon/psk.txt' \
    --exclude='./private/etc/racoon/remote/anonymous.conf' \
    -C "$source" . |
    remote "set -euo pipefail; sudo LIBGUESTFS_BACKEND=direct guestfish --rw \
      --format=raw -a '$REMOTE_OUT' run : mount /dev/sda / : \
      mkdir-p '$destination' : tar-in - '$destination'"
}

stream_secure_overlay() {
  [[ -n "$SECURE_OVERLAY" ]] || return 0
  echo "=== streaming root-only macOS-readable files from $SECURE_OVERLAY ==="
  tar -cpf - --disable-copyfile --no-acls --no-xattrs \
    --uid 0 --gid 0 --uname 0 --gname 0 \
    -C "$SECURE_OVERLAY" . |
    remote "set -euo pipefail; sudo LIBGUESTFS_BACKEND=direct guestfish --rw \
      --format=raw -a '$REMOTE_OUT' run : mount /dev/sda / : tar-in - /"
}

echo "=== creating fresh non-journaled HFSX $SIZE on orb ==="
remote "set -euo pipefail; rm -f '$REMOTE_OUT'; truncate -s '$SIZE' '$REMOTE_OUT'; \
  mkfs.hfsplus -s -v iOSRoot '$REMOTE_OUT'"

stream_tree "$OS_VOLUME" /
stream_secure_overlay
stream_tree "$SYS_VOLUME" /private/preboot/Cryptexes/OS
stream_tree "$APP_VOLUME" /private/preboot/Cryptexes/App

echo "=== applying SWEET and alioth v15 mutations ==="
remote "set -euo pipefail; rm -rf '$REMOTE_MOUNT'; mkdir -p '$REMOTE_MOUNT'; \
  cleanup() { sudo guestunmount '$REMOTE_MOUNT' >/dev/null 2>&1 || \
    sudo fusermount3 -u '$REMOTE_MOUNT' >/dev/null 2>&1 || true; }; \
  trap cleanup EXIT; \
  LIBGUESTFS_BACKEND=direct sudo -E guestmount --rw --format=raw \
    -a '$REMOTE_OUT' -m /dev/sda '$REMOTE_MOUNT'; \
  sudo python3 '$REMOTE_POST' '$REMOTE_MOUNT'; \
  sudo python3 '$REMOTE_WALLPAPER' '$REMOTE_MOUNT'; \
  sudo python3 '$REMOTE_VERIFY' '$REMOTE_MOUNT'; \
  sudo guestunmount '$REMOTE_MOUNT'; trap - EXIT; rmdir '$REMOTE_MOUNT'"

echo "=== HFSX pre-journal validation ==="
remote "set -euo pipefail; fsck.hfsplus -f -y '$REMOTE_OUT' >/tmp/alioth-fsck.$$.log 2>&1 || \
  { cat /tmp/alioth-fsck.$$.log; exit 1; }; grep -v 'Illegal name' /tmp/alioth-fsck.$$.log; \
  fsck.hfsplus -f -n '$REMOTE_OUT'; rm -f /tmp/alioth-fsck.$$.log"

echo "IOS_ROOT_STAGING_OK"
echo "Next: scripts/finish-alioth-ios-root-macos.sh $OUT"
