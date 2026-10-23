#!/usr/bin/env python3
"""Fail closed unless a mounted root has the alioth iOS 20H392 v15 contracts."""

from __future__ import annotations

import glob
import os
import plistlib
import stat
import sys
import time


BUILD = "20H392"
MODEL = "iPhone10,3"
NETWORK_SERVICE_ID = "9B1D2119-5F86-51C8-8C42-27CB6B8548A3"
NETWORK_SET_ID = "5070B4FF-4B22-53D9-949E-794F0BCFD60D"


def fail(message: str) -> None:
    raise SystemExit(f"root verification: {message}")


def path(root: str, relative: str) -> str:
    result = os.path.join(root, relative)
    if not os.path.lexists(result):
        fail(f"missing /{relative}")
    return result


def check_stat(
    root: str,
    relative: str,
    *,
    uid: int | None = None,
    gid: int | None = None,
    mode: int | None = None,
    directory: bool | None = None,
) -> os.stat_result:
    item = path(root, relative)
    info = os.lstat(item)
    if stat.S_ISLNK(info.st_mode):
        fail(f"/{relative} is a symlink")
    if directory is True and not stat.S_ISDIR(info.st_mode):
        fail(f"/{relative} is not a directory")
    if directory is False and not stat.S_ISREG(info.st_mode):
        fail(f"/{relative} is not a regular file")
    if uid is not None and info.st_uid != uid:
        fail(f"/{relative} uid is {info.st_uid}, expected {uid}")
    if gid is not None and info.st_gid != gid:
        fail(f"/{relative} gid is {info.st_gid}, expected {gid}")
    if mode is not None and stat.S_IMODE(info.st_mode) != mode:
        fail(f"/{relative} mode is {stat.S_IMODE(info.st_mode):o}, expected {mode:o}")
    return info


def load_plist(item: str) -> object:
    last_error: Exception | None = None
    for _ in range(8):
        try:
            with open(item, "rb") as stream:
                return plistlib.load(stream)
        except Exception as error:
            last_error = error
            time.sleep(0.1)
    fail(f"cannot read plist /{os.path.relpath(item, ROOT)}: {last_error}")


def verify_launchd() -> None:
    check_stat(
        ROOT,
        "System/Library/xpc/launchd.plist",
        uid=0,
        gid=80,
        directory=False,
    )
    entries = load_plist(path(ROOT, "System/Library/xpc/launchd.plist"))
    if not isinstance(entries, dict) or not isinstance(entries.get("LaunchDaemons"), dict):
        fail("launchd.plist has no LaunchDaemons dictionary")
    jobs = entries["LaunchDaemons"]
    if any(isinstance(job, dict) and job.get("Label") == "com.apple.CommCenter" for job in jobs.values()):
        fail("com.apple.CommCenter is still installed")
    old_panic_flags = sum(
        isinstance(job, dict) and "_PanicOnCrash" in job for job in jobs.values()
    )
    new_panic_flags = sum(
        isinstance(job, dict) and "_PanicOnCrasX" in job for job in jobs.values()
    )
    if old_panic_flags:
        fail(f"{old_panic_flags} launchd jobs still use _PanicOnCrash")
    if new_panic_flags != 7:
        fail(f"launchd has {new_panic_flags} _PanicOnCrasX jobs, expected 7")


def verify_ownership() -> None:
    check_stat(ROOT, "sbin/launchd", uid=0, gid=0, directory=False)
    check_stat(ROOT, "usr/lib/dyld", uid=0, gid=0, directory=False)
    check_stat(ROOT, "private/var/db", uid=0, gid=0, directory=True)
    check_stat(ROOT, "private/var/db/diagnostics", uid=272, gid=272, directory=True)
    check_stat(ROOT, "private/var/db/uuidtext", uid=272, gid=272, directory=True)
    check_stat(
        ROOT,
        "private/var/db/diagnostics/timesync",
        uid=272,
        gid=272,
        directory=True,
    )
    check_stat(ROOT, "private/var/root", uid=0, gid=0, mode=0o750, directory=True)
    check_stat(ROOT, "private/var/audit", uid=0, gid=0, mode=0o700, directory=True)
    check_stat(
        ROOT,
        "private/var/keybags/systembag.kb",
        uid=0,
        gid=0,
        mode=0o600,
        directory=False,
    )
    for relative, uid in (
        ("private/var/containers/Bundle", 33),
        ("private/var/containers/Data/System", 0),
        ("private/var/containers/Shared/SystemGroup", 0),
        ("private/var/containers/Temp", 0),
    ):
        check_stat(ROOT, relative, uid=uid, gid=uid, directory=True)


def verify_cryptex_links() -> None:
    check_stat(
        ROOT,
        "private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld",
        directory=True,
    )
    check_stat(ROOT, "private/preboot/Cryptexes/App/System/Library", directory=True)
    dyld_cache = path(ROOT, "System/Library/Caches/com.apple.dyld")
    files = [
        entry
        for entry in os.scandir(dyld_cache)
        if entry.is_file(follow_symlinks=False)
    ]
    if len(files) != 44:
        fail(f"dyld cache has {len(files)} linked files, expected 44")
    manifest = load_plist(path(ROOT, "private/var/db/alioth-ios-root-manifest.plist"))
    if not isinstance(manifest, dict):
        fail("root manifest is not a dictionary")
    expected = {
        "Build": BUILD,
        "ProductType": MODEL,
        "CryptexFilesLinked": 19,
        "DyldCacheFilesLinked": 44,
    }
    for key, value in expected.items():
        if manifest.get(key) != value:
            fail(f"root manifest {key}={manifest.get(key)!r}, expected {value!r}")


def verify_audio_and_media() -> None:
    enabled_plugin = os.path.join(
        ROOT, "Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin"
    )
    if os.path.lexists(enabled_plugin):
        fail("VirtualAudio.plugin is still enabled")
    check_stat(
        ROOT,
        "Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin",
        directory=True,
    )
    configs = sorted(
        glob.glob(
            path(
                ROOT,
                "System/Library/Frameworks/MediaToolbox.framework",
            )
            + "/*/SubServerConfiguration.plist"
        )
    )
    if len(configs) != 3:
        fail(f"found {len(configs)} MediaToolbox configurations, expected 3")
    for config in configs:
        values = load_plist(config)
        if not isinstance(values, dict):
            fail(f"invalid MediaToolbox configuration /{os.path.relpath(config, ROOT)}")
        if values.get("StartRecorderServer") is not False:
            fail(f"recorder server remains enabled in /{os.path.relpath(config, ROOT)}")


def verify_network() -> None:
    relative = "private/var/preferences/SystemConfiguration/preferences.plist"
    check_stat(ROOT, relative, uid=0, gid=0, mode=0o644, directory=False)
    values = load_plist(path(ROOT, relative))
    if not isinstance(values, dict):
        fail("SystemConfiguration preferences are not a dictionary")
    if values.get("CurrentSet") != f"/Sets/{NETWORK_SET_ID}":
        fail("v15 network set is not selected")
    service = values.get("NetworkServices", {}).get(NETWORK_SERVICE_ID, {})
    interface = service.get("Interface", {})
    ipv4 = service.get("IPv4", {})
    expected = {
        "DeviceName": "ue0",
        "Hardware": "Ethernet",
        "Type": "Ethernet",
        "UserDefinedName": "USB Ethernet",
    }
    if interface != expected:
        fail(f"v15 interface is {interface!r}")
    if ipv4.get("ConfigMethod") != "Manual":
        fail("v15 interface is not manual IPv4")
    if ipv4.get("Addresses") != ["192.168.7.2"] or ipv4.get("Router") != "192.168.7.1":
        fail("v15 IPv4 address/router is wrong")
    if service.get("DNS", {}).get("ServerAddresses") != ["1.1.1.1", "8.8.8.8"]:
        fail("v15 DNS servers are wrong")


def verify_user_preferences() -> None:
    base = "private/var/mobile/Library/Preferences"
    checks = (
        ("com.apple.springboard.plist", "BKNoWatchdogs", True),
        ("com.apple.purplebuddy.plist", "SetupDone", True),
        ("com.apple.purplebuddy.plist", "SetupFinishedAllSteps", True),
        (
            "com.apple.managedconfiguration.notbackedup.plist",
            "LockdownActivationIndicatesCloudConfigurationAvailable",
            False,
        ),
    )
    for filename, key, value in checks:
        relative = f"{base}/{filename}"
        check_stat(ROOT, relative, uid=501, gid=501, mode=0o600, directory=False)
        values = load_plist(path(ROOT, relative))
        if not isinstance(values, dict) or values.get(key) is not value:
            fail(f"/{relative} has unexpected {key}")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: verify-alioth-ios-root.py MOUNTED_ROOT")
    global ROOT
    ROOT = os.path.realpath(sys.argv[1])
    if not os.path.isdir(ROOT):
        fail(f"not a directory: {ROOT}")
    verify_launchd()
    verify_ownership()
    verify_cryptex_links()
    verify_audio_and_media()
    verify_network()
    verify_user_preferences()
    print(
        "IOS_ROOT_VERIFY_OK: launchd, ownership, cryptex links, "
        "audio/media, v15 network and setup preferences"
    )


if __name__ == "__main__":
    main()
