#!/usr/bin/env bash
# Add the HFS+ journal with macOS and emit the Android sparse image.
#
# Linux hfsplus mounts journaled volumes read-only, so
# build-alioth-ios-root.sh deliberately populates a non-journaled HFSX image
# on orb.  macOS can enable the journal without sudo after attaching the raw
# file as a user-owned disk image.  The final image is converted to Android
# sparse format on orb, where img2simg is installed.
set -euo pipefail

IMG=${1:?usage: finish-alioth-ios-root-macos.sh IMAGE}
IMG=$(cd "$(dirname "$IMG")" && pwd)/$(basename "$IMG")
[[ -f "$IMG" ]] || { echo "missing image: $IMG" >&2; exit 2; }

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
      echo "image must be below the shared Bridge volume: $value" >&2
      return 1
      ;;
  esac
}

REMOTE_IMG=$(shared_remote_path "$IMG")

command -v hdiutil >/dev/null
command -v diskutil >/dev/null
command -v ssh >/dev/null

attach_output=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$IMG")
dev=$(printf '%s\n' "$attach_output" | awk 'NR == 1 { print $1 }')
[[ -b "$dev" ]] || { echo "could not attach $IMG" >&2; exit 1; }

detach() {
  hdiutil detach "$dev" >/dev/null 2>&1 || true
}
trap detach EXIT

echo "=== enabling native HFS+ journal on $dev ==="
diskutil enableJournal "$dev"
# guestfish can leave directory link-count flags stale even though all file
# data and catalog entries are present. Repair that metadata before converting.
fsck_hfs -fy "$dev"
fsck_hfs -fn "$dev"
diskutil info "$dev" | grep -E 'File System Personality:|Journal:|Media Read-Only:'
sync
detach
trap - EXIT

SPARSE=${IMG%.img}.simg
REMOTE_SPARSE=$(shared_remote_path "$SPARSE")
echo "=== converting to Android sparse image on orb ==="
ssh orb "set -euo pipefail; img2simg '$REMOTE_IMG' '$REMOTE_SPARSE'"

echo "=== final artifact report ==="
shasum -a 256 "$IMG"
ssh orb "set -euo pipefail; sha256sum '$REMOTE_SPARSE'; ls -lh '$REMOTE_IMG' '$REMOTE_SPARSE'"
echo "IOS_ROOT_BUILD_OK"
