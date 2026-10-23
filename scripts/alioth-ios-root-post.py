#!/usr/bin/env python3
"""Apply alioth-specific metadata and v15 network data to a mounted iOS root."""

from __future__ import annotations

import os
import plistlib
import glob
import shutil
import stat
import sys
import time
import uuid


BUILD = "20H392"
MODEL = "iPhone10,3"
NETWORK_SERVICE_ID = "9B1D2119-5F86-51C8-8C42-27CB6B8548A3"
NETWORK_SET_ID = "5070B4FF-4B22-53D9-949E-794F0BCFD60D"


def fail(message: str) -> None:
    raise SystemExit(f"alioth root: {message}")


def require(root: str, relative: str) -> str:
    path = os.path.join(root, relative)
    if not os.path.exists(path):
        fail(f"missing required path: /{relative}")
    return path


def ensure_dir(path: str, uid: int, gid: int, mode: int) -> None:
    os.makedirs(path, exist_ok=True)
    os.chown(path, uid, gid)
    os.chmod(path, mode)


def write_plist(path: str, value: object, uid: int, gid: int, mode: int) -> None:
    with open(path, "wb") as stream:
        plistlib.dump(value, stream, fmt=plistlib.FMT_BINARY, sort_keys=True)
    os.chown(path, uid, gid)
    os.chmod(path, mode)


def load_plist(path: str) -> dict[str, object]:
    last_error: Exception | None = None
    for _ in range(4):
        try:
            with open(path, "rb") as stream:
                return plistlib.load(stream)
        except Exception as error:
            last_error = error
            time.sleep(0.1)
    raise SystemExit(f"alioth root: cannot read plist /{os.path.relpath(path, ROOT)}: {last_error}")


def merge_preference(root: str, name: str, values: dict[str, object]) -> None:
    directory = os.path.join(root, "private/var/mobile/Library/Preferences")
    ensure_dir(directory, 501, 501, 0o755)
    path = os.path.join(directory, name + ".plist")
    current = load_plist(path) if os.path.exists(path) else {}
    current.update(values)
    write_plist(path, current, 501, 501, 0o600)
    print(f"preference: {name}")


def patch_launchd(root: str) -> None:
    path = os.path.join(root, "System/Library/xpc/launchd.plist")
    info = os.stat(path)
    entries = load_plist(path)
    jobs = entries["LaunchDaemons"]
    renamed = 0
    for job in jobs.values():
        if "_PanicOnCrash" in job:
            job["_PanicOnCrasX"] = job.pop("_PanicOnCrash")
            renamed += 1
    removed = [key for key, job in jobs.items() if job.get("Label") == "com.apple.CommCenter"]
    for key in removed:
        del jobs[key]
    write_plist(path, entries, info.st_uid, info.st_gid, stat.S_IMODE(info.st_mode))
    print(f"launchd: {renamed} panic flags, {len(removed)} CommCenter jobs")


def install_containers(root: str) -> None:
    base = os.path.join(root, "private/var/containers")
    for relative, uid in (
        ("Bundle", 33),
        ("Data/System", 0),
        ("Shared/SystemGroup", 0),
        ("Temp", 0),
    ):
        ensure_dir(os.path.join(base, relative), uid, uid, 0o755)


def find_container(root: str, identifier: str) -> str | None:
    pattern = os.path.join(
        root,
        "private/var/containers/Data/System/*",
        ".com.apple.mobile_container_manager.metadata.plist",
    )
    for metadata in glob.glob(pattern):
        values = load_plist(metadata)
        if values.get("MCMMetadataIdentifier") == identifier:
            return os.path.dirname(metadata)
    return None


def patch_activation(root: str) -> None:
    container = find_container(root, "com.apple.mobileactivationd")
    if container is None:
        print("activation: deferred (mobileactivationd container is created at first boot)")
        return
    directory = os.path.join(container, "Library/internal")
    ensure_dir(directory, 501, 501, 0o755)
    write_plist(
        os.path.join(directory, "data_ark.plist"),
        {
            "-ActivationState": "Activated",
            "-BrickState": False,
            "-BuildVersion": BUILD,
            "-ProductVersion": "16.7.16",
            "-DataMigrationCompleted": True,
            "-PreSydroDataMigrationCompleted": True,
        },
        501,
        501,
        0o600,
    )
    print(f"activation: {os.path.basename(container)}")


def patch_audio_and_media(root: str) -> None:
    source = os.path.join(root, "Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin")
    if os.path.exists(source):
        destination = os.path.join(root, "Library/Audio/Plug-Ins/HAL.disabled")
        ensure_dir(destination, 0, 0, 0o755)
        shutil.move(source, destination)
        print("audio: VirtualAudio.plugin disabled")

    pattern = os.path.join(
        root,
        "System/Library/Frameworks/MediaToolbox.framework/*/SubServerConfiguration.plist",
    )
    for path in sorted(glob.glob(pattern)):
        info = os.stat(path)
        values = load_plist(path)
        values["StartRecorderServer"] = False
        write_plist(path, values, info.st_uid, info.st_gid, stat.S_IMODE(info.st_mode))
        print(f"media: StartRecorderServer=false /{os.path.relpath(path, root)}")


def install_user_preferences(root: str) -> None:
    merge_preference(
        root,
        "com.apple.migration",
        {
            "LastSystemVersion": BUILD,
            "DMUserDataDisposition": {
                "SyntheticDidUpgrade": False,
                "buildVersion": BUILD,
            },
        },
    )
    merge_preference(
        root,
        "com.apple.springboard",
        {
            "BKNoWatchdogs": True,
            "FBLaunchWatchdogExceptions": {"com.apple.purplebuddy": 100.0},
            "MCForceShowCloudConfigurationUI": False,
        },
    )
    merge_preference(
        root,
        "com.apple.purplebuddy",
        {
            "SetupDone": True,
            "SetupFinishedAllSteps": True,
            "SetupVersion": 11,
            "daemonizeNeedsToRun": False,
        },
    )
    merge_preference(
        root,
        "com.apple.purplebuddy.notbackedup",
        {
            key: False
            for key in (
                "ShouldRunDiagnosticsMiniBuddy",
                "ShouldRunHSA2MiniBuddy",
                "ShouldRunKeychainSyncMiniBuddy",
                "ShouldRunMesaMiniBuddy",
                "ShouldRunPaymentMiniBuddy",
                "ShouldRunStandardMiniBuddy",
                "SilentICDPUpgrade",
            )
        },
    )
    merge_preference(
        root,
        "com.apple.managedconfiguration.notbackedup",
        {"LockdownActivationIndicatesCloudConfigurationAvailable": False},
    )


def link_missing_tree(root: str, source: str, skip_prefix: str = "") -> int:
    """Hard-link source-only files into the root tree without following links."""
    linked = 0

    def visit(directory: str) -> None:
        nonlocal linked
        with os.scandir(directory) as entries:
            for entry in entries:
                source_path = entry.path
                relative = os.path.relpath(source_path, source)
                if skip_prefix and (relative == skip_prefix or relative.startswith(skip_prefix + "/")):
                    continue
                if entry.is_dir(follow_symlinks=False):
                    visit(source_path)
                    continue

                destination = os.path.join(root, relative)
                if os.path.lexists(destination):
                    continue
                ensure_dir(os.path.dirname(destination), 0, 0, 0o755)
                if entry.is_symlink():
                    os.symlink(os.readlink(source_path), destination)
                else:
                    os.link(source_path, destination)
                linked += 1

    visit(source)
    return linked


def link_dyld_cache(root: str) -> int:
    source = os.path.join(
        root, "private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld"
    )
    if not os.path.isdir(source):
        fail("SystemOS dyld cache directory is absent")
    destination = os.path.join(root, "System/Library/Caches/com.apple.dyld")
    ensure_dir(destination, 0, 0, 0o755)

    linked = 0
    with os.scandir(source) as entries:
        for entry in entries:
            target = os.path.join(destination, entry.name)
            if os.path.lexists(target):
                continue
            if entry.is_dir(follow_symlinks=False):
                continue
            if entry.is_symlink():
                os.symlink(os.readlink(entry.path), target)
            else:
                os.link(entry.path, target)
            linked += 1
    if linked == 0:
        fail("no dyld cache files were linked")
    return linked


def install_logd_and_keybag(root: str) -> None:
    database = os.path.join(root, "private/var/db")
    ensure_dir(database, 0, 0, 0o755)
    for relative, mode in (
        ("diagnostics", 0o755),
        ("uuidtext", 0o755),
        ("diagnostics/timesync", 0o755),
    ):
        directory = os.path.join(database, relative)
        ensure_dir(directory, 272, 272, mode)
        marker = os.path.join(directory, ".migrated")
        with open(marker, "wb"):
            pass
        os.chown(marker, 272, 272)
        os.chmod(marker, 0o644)

    keybags = os.path.join(root, "private/var/keybags")
    ensure_dir(keybags, 0, 0, 0o700)
    keybag = os.path.join(keybags, "systembag.kb")
    write_plist(keybag, {"KeyBagKeys": b"SWEET"}, 0, 0, 0o600)

    # These two trees are root-only in the OS volume, so the unprivileged
    # macOS tar stream intentionally skips them. Recreate the empty runtime
    # directories with their Apple owners and modes.
    ensure_dir(os.path.join(root, "private/var/root"), 0, 0, 0o750)
    ensure_dir(os.path.join(root, "private/var/audit"), 0, 0, 0o700)


def install_system_configuration(root: str) -> None:
    directory = os.path.join(root, "private/var/preferences/SystemConfiguration")
    ensure_dir(directory, 0, 0, 0o755)
    path = os.path.join(directory, "preferences.plist")
    current: dict[str, object] = {}
    if os.path.exists(path):
        with open(path, "rb") as stream:
            current = plistlib.load(stream)

    service = {
        "DNS": {"ServerAddresses": ["1.1.1.1", "8.8.8.8"]},
        "Interface": {
            "DeviceName": "ue0",
            "Hardware": "Ethernet",
            "Type": "Ethernet",
            "UserDefinedName": "USB Ethernet",
        },
        "IPv4": {
            "Addresses": ["192.168.7.2"],
            "ConfigMethod": "Manual",
            "Router": "192.168.7.1",
            "SubnetMasks": ["255.255.255.0"],
        },
        "IPv6": {"ConfigMethod": "Automatic"},
        "Proxies": {},
        "SMB": {},
        "UserDefinedName": "USB Ethernet",
    }
    network_set = {
        "Network": {
            "Global": {"IPv4": {"ServiceOrder": [NETWORK_SERVICE_ID]}},
            "Service": {
                NETWORK_SERVICE_ID: {
                    "__LINK__": f"/NetworkServices/{NETWORK_SERVICE_ID}"
                }
            },
        },
        "UserDefinedName": "Automatic",
    }
    current["__VERSION__"] = 20191120
    current["CurrentSet"] = f"/Sets/{NETWORK_SET_ID}"
    current["Model"] = MODEL
    current["NetworkServices"] = {
        NETWORK_SERVICE_ID: service,
    }
    current["Sets"] = {
        NETWORK_SET_ID: network_set,
    }
    current.setdefault(
        "System",
        {
            "Network": {
                "HostNames": {
                    "LocalHostName": "alioth",
                }
            }
        },
    )
    write_plist(path, current, 0, 0, 0o644)
    print(f"SystemConfiguration: USB Ethernet service {uuid.UUID(NETWORK_SERVICE_ID)}")


def verify_activation(root: str) -> None:
    container_root = os.path.join(root, "private/var/containers/Data/System")
    try:
        containers = sorted(os.scandir(container_root), key=lambda entry: entry.name)
    except FileNotFoundError:
        print("activation: deferred (fresh HFS root has no Data volume containers)")
        return

    for container in containers:
        metadata = os.path.join(
            container.path, ".com.apple.mobile_container_manager.metadata.plist"
        )
        if not container.is_dir(follow_symlinks=False) or not os.path.exists(metadata):
            continue
        with open(metadata, "rb") as stream:
            values = plistlib.load(stream)
        if values.get("MCMMetadataIdentifier") != "com.apple.mobileactivationd":
            continue
        data_ark = os.path.join(container.path, "Library/internal/data_ark.plist")
        if not os.path.exists(data_ark):
            fail("mobileactivationd container exists but its data ark is absent")
        with open(data_ark, "rb") as stream:
            activation = plistlib.load(stream)
        if activation.get("-ActivationState") != "Activated":
            fail("mobileactivationd data ark is not Activated")
        print(f"activation: {os.path.basename(container.path)}")
        return
    print("activation: deferred (mobileactivationd container is created at first boot)")


def write_manifest(root: str, linked_total: int, linked_dyld: int) -> None:
    directory = os.path.join(root, "private/var/db")
    ensure_dir(directory, 0, 0, 0o755)
    manifest = {
        "Build": BUILD,
        "NetworkServiceID": NETWORK_SERVICE_ID,
        "NetworkSetID": NETWORK_SET_ID,
        "ProductType": MODEL,
        "CryptexFilesLinked": linked_total,
        "DyldCacheFilesLinked": linked_dyld,
    }
    write_plist(
        os.path.join(directory, "alioth-ios-root-manifest.plist"),
        manifest,
        0,
        0,
        0o644,
    )


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: alioth-ios-root-post.py MOUNTED_ROOT")
    global ROOT
    root = os.path.realpath(sys.argv[1])
    ROOT = root
    if not os.path.isdir(root):
        fail(f"not a directory: {root}")

    for relative in (
        "System/Library/xpc/launchd.plist",
        "sbin/launchd",
        "usr/lib/dyld",
        "private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld",
        "private/preboot/Cryptexes/App/System/Library",
    ):
        require(root, relative)

    patch_launchd(root)
    install_containers(root)
    patch_activation(root)
    patch_audio_and_media(root)
    install_user_preferences(root)
    linked_dyld = link_dyld_cache(root)
    linked_os = link_missing_tree(
        root,
        os.path.join(root, "private/preboot/Cryptexes/OS"),
        "System/Library/Caches/com.apple.dyld",
    )
    linked_app = link_missing_tree(
        root, os.path.join(root, "private/preboot/Cryptexes/App")
    )
    install_logd_and_keybag(root)
    install_system_configuration(root)
    verify_activation(root)
    write_manifest(root, linked_os + linked_app, linked_dyld)

    for relative, expected_uid, expected_gid in (
        ("System/Library/xpc/launchd.plist", 0, 80),
        ("sbin/launchd", 0, 0),
        ("System/Library/Caches/com.apple.dyld", 0, 0),
        ("private/var/keybags/systembag.kb", 0, 0),
        ("private/var/db", 0, 0),
        ("private/var/root", 0, 0),
        ("private/var/audit", 0, 0),
        ("private/var/preferences/SystemConfiguration/preferences.plist", 0, 0),
    ):
        info = os.lstat(os.path.join(root, relative))
        if (info.st_uid, info.st_gid) != (expected_uid, expected_gid):
            fail(f"wrong ownership on /{relative}: {info.st_uid}:{info.st_gid}")
        if stat.S_ISLNK(info.st_mode):
            fail(f"unexpected symlink at /{relative}")

    print(
        "alioth root post-processing OK: "
        f"{linked_os + linked_app} cryptex entries, {linked_dyld} dyld cache files"
    )


if __name__ == "__main__":
    main()
