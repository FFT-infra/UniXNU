#!/usr/bin/env bash
# Build the alioth iOS 16.7.16 root image on a Linux host.
#
# The normal ios-sweet builder mounts the image with a loop device. That is
# unavailable in the shared development container, so this wrapper uses
# libguestfs' FUSE mount, which preserves numeric ownership and hard links
# without exposing a loop device.  Run it on the orb host, not on macOS:
#
#   scripts/build-alioth-ios-root.sh [OUTPUT] [SIZE]
#
# The three source images must be the exact IPSW members documented in PLAN.md.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:-"$ROOT/out/alioth/userdata/ios16-root-20H392.img"}
SIZE=${2:-16G}
OS=${OS:-"$ROOT/out/ipsw/20H392/087-86698-044.dmg"}
SYSOS=${SYSOS:-"$ROOT/out/ipsw/20H392/087-86593-047.dmg"}
APPOS=${APPOS:-"$ROOT/out/ipsw/20H392/087-86715-047.dmg"}
IOS_SWEET=${IOS_SWEET:-/home/xinyang/Lab/Bridge/WorkSpace/alioth/ios-sweet}
TWEAKS="$IOS_SWEET/tools/sweet-root-tweaks.py"

for path in "$OS" "$SYSOS" "$APPOS" "$TWEAKS"; do
  [[ -e "$path" ]] || { echo "missing input: $path" >&2; exit 2; }
done
for tool in apfs-fuse guestmount guestunmount mkfs.hfsplus rsync fsck.hfsplus img2simg; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool" >&2; exit 2; }
done
[[ $SIZE =~ ^[0-9]+([GgMm])$ ]] || { echo "unsupported image size: $SIZE" >&2; exit 2; }

mkdir -p "$(dirname "$OUT")"
if [[ -e "$OUT" && ${ALLOW_REBUILD:-0} != 1 ]]; then
  echo "refusing to overwrite $OUT (set ALLOW_REBUILD=1 for a generated artifact)" >&2
  exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/alioth-ios-root.XXXXXX")
os_mnt="$work/os"
sys_mnt="$work/sysos"
app_mnt="$work/appos"
root_mnt="$work/root"
mkdir -p "$os_mnt" "$sys_mnt" "$app_mnt" "$root_mnt"

cleanup() {
  set +e
  for m in "$root_mnt" "$os_mnt" "$sys_mnt" "$app_mnt"; do
    if mountpoint -q "$m" 2>/dev/null; then
      sudo guestunmount "$m" 2>/dev/null || sudo fusermount3 -u "$m" 2>/dev/null || true
    fi
  done
  rmdir "$root_mnt" "$os_mnt" "$sys_mnt" "$app_mnt" "$work" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== mounting IPSW APFS inputs ==="
sudo apfs-fuse "$OS" "$os_mnt" -o uid=0,gid=0
sudo apfs-fuse "$SYSOS" "$sys_mnt" -o uid=0,gid=0
sudo apfs-fuse "$APPOS" "$app_mnt" -o uid=0,gid=0
for m in "$os_mnt" "$sys_mnt" "$app_mnt"; do
  sudo test -d "$m/root" || { echo "missing root/ in $m" >&2; exit 1; }
done

echo "=== creating case-sensitive HFSX $SIZE (journal is added after population) ==="
rm -f "$OUT"
truncate -s "$SIZE" "$OUT"
# Linux's hfsplus driver refuses to mount a journaled volume read-write.
# Populate a non-journaled HFSX here, then use
# finish-alioth-ios-root-macos.sh to add the journal with macOS's native
# implementation before creating the sparse Android image.
mkfs.hfsplus -s -v iOSRoot "$OUT"

echo "=== mounting HFSX through libguestfs ==="
LIBGUESTFS_BACKEND=direct sudo -E guestmount --rw --format=raw -a "$OUT" -m /dev/sda "$root_mnt"

echo "=== copying OS ==="
sudo rsync -aH --numeric-ids "$os_mnt/root/" "$root_mnt/"
sudo mkdir -p "$root_mnt/private/preboot/Cryptexes/OS"
sudo mkdir -p "$root_mnt/private/preboot/Cryptexes/App"
echo "=== copying SystemOS cryptex ==="
sudo rsync -aH --numeric-ids "$sys_mnt/root/" "$root_mnt/private/preboot/Cryptexes/OS/"
echo "=== copying AppOS cryptex ==="
sudo rsync -aH --numeric-ids "$app_mnt/root/" "$root_mnt/private/preboot/Cryptexes/App/"

echo "=== linking dyld cache ==="
dsc="$root_mnt/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld"
sudo mkdir -p "$root_mnt/System/Library/Caches/com.apple.dyld"
if [[ -d "$dsc" ]]; then
  for f in "$dsc"/*; do
    [[ -e "$f" ]] || continue
    sudo ln "$f" "$root_mnt/System/Library/Caches/com.apple.dyld/$(basename "$f")"
  done
fi

echo "=== linking cryptex files without OS counterparts ==="
for c in OS App; do
  base="$root_mnt/private/preboot/Cryptexes/$c"
  [[ -d "$base" ]] || continue
  (cd "$base" && sudo find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
  while IFS= read -r f; do
    f=${f#./}
    dst="$root_mnt/$f"
    src="$base/$f"
    if ! sudo test -e "$dst" && ! sudo test -L "$dst"; then
      sudo mkdir -p "$(dirname "$dst")"
      if sudo test -L "$src"; then
        sudo cp -P "$src" "$dst"
      else
        sudo ln "$src" "$dst"
      fi
    fi
  done
done

echo "=== creating daemon state directories and plain keybag ==="
db="$root_mnt/private/var/db"
sudo install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
sudo install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do
  sudo install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"
done
sudo install -d -m 0700 -o 0 -g 0 "$root_mnt/private/var/keybags"
printf '%s\n' \
  '<?xml version="1.0" encoding="UTF-8"?>' \
  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
  '<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' |
  sudo install -m 0600 -o 0 -g 0 /dev/stdin "$root_mnt/private/var/keybags/systembag.kb"

echo "=== applying SWEET root tweaks ==="
sudo python3 "$TWEAKS" "$root_mnt"
sudo guestunmount "$root_mnt"

echo "=== HFSX check before journal conversion ==="
fsck.hfsplus -f -y "$OUT" | grep -v 'Illegal name' || true
fsck.hfsplus -f -n "$OUT"

echo "=== artifact report ==="
sha256sum "$OUT"
ls -lh "$OUT"
echo "IOS_ROOT_BUILD_PREJOURNAL_OK"
echo "Next, run scripts/finish-alioth-ios-root-macos.sh $OUT"
