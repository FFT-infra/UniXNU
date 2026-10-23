# alioth iOS bring-up — 2026-10-24

Record of the v24–v27 diagnostic boot series and the first working SpringBoard UI.
Per-version raw logs live next to each image under
`out/alioth/ios-sweet-0081/diag-markers-vNN/` (git-ignored); this file is the
tracked summary.

## Result

`BSD root: ufsroot0` + HFSX mount + launchd PID 1 + **SpringBoard lock screen**,
in one boot, from a persistent UFS root on `userdata`.

Screen: `Wednesday, December 31  4:17` — SpringBoard's own lock screen, drawn
with CoreAnimation's software renderer. No GPU driver involved.

## Chain that now works

```
UEFI -> DarwinBoot -> Sweet kernel -> UFS root (polled UFSHCI) -> HFSX mount
  -> launchd PID 1 -> boot tasks (keybag, fips, tzinit, sysstatuscheck,
     launchd_cache_loader) -> Early boot complete -> xpcproxy -> SpringBoard
  -> backboardd -> Apple logo -> lock screen
```

## Two root causes found and fixed

### 1. Timer chain (v25)

The v20/v23 stalls were blamed on a dead virtual timer. That was wrong.

`CNTV_CVAL_EL0` advances across a 2 s window (`moved=0x1920b849`), and only
`rtclock_intr` rewrites CVAL, so timer interrupts were being delivered and
handled all along. `delay(1)` returns. `DAIF=0x200` (only D masked).

`GICR_IGROUPR0` writes are silently dropped (write `0xFFFFFFFF`, read back 0)
because `GICD_CTLR.DS=0`. That is normal for two-security-state hardware, not a
fault, and it is not what blocked the PPI.

Both earlier conclusions are retracted.

### 2. Root directory mode (v27)

`CRASH SpringBoard reason ns 35 code 0x2: failed to open root directory: /: 13`
(`OS_REASON_LIBIGNITION`, ignition stage 2, `EACCES`).

`/` in the flashed image was `0o40700` root:wheel. launchd (root) opened it
fine; SpringBoard (uid 501) got EACCES.

Cause: `scripts/build-alioth-ios-root-macos.sh` streams the OS volume with
`tar -cpf - ... -C "$source" .` then `tar-in - /`. tar carries the source
directory's own mode (`drwx------`), overwriting the `mkfs.hfsplus` default.
`mkfs.hfsplus` alone produces `0o40755`; the older `rsync -aH .../root/ "$HFS/"`
path never touched the target directory's own mode, which is why earlier images
were correct.

Fix: set fileMode to `0o40755`, then write it back through a real mount
(`diskutil mount` → `chmod 755` → `unmount`), then `img2simg` and flash the
whole partition.

**Hard-won rule: never patch a volume that has already been booted with a raw
block write.** The first attempt flashed a single 4 KB sparse block at
`block 181635`, the location of the root catalog record in the build image. On
the device that block had already been rewritten by HFS during an earlier
read-write boot, so the write destroyed B-tree node 1:

```
hfs_swap_HFSPlusBTInternalNode: catalog key #24 invalid length (0)
hfs_mark_inconsistent: Runtime corruption detected on . fsck will be forced on next mount.
hfs_mountfsplus: cat_idlookup returned (5) getting rootfolder
cannot mount root, errno = 19
```

The block on disk is not the block in the build image once the volume has been
mounted read-write.

## UFS stalls (v26)

`AL23x`/`AL23y` brackets around `ufs_rd(REG_UTRLDBR)` ran to `seq=90305` with
every pair matched, `dbr=0` on every read, `polls=0`. The loop-top register read
was never the stall point. The v20 `seq=796` and v23 `seq=9595` stalls were
downstream effects of the timer state in those builds and no longer reproduce.

## Diagnostic output must be off for real boots

With `ufsdiagquiet=0` the kernel prints ~8 lines per 4 KB UFS read. A boot reads
gigabytes (the dyld shared cache alone is 6.5 GB), so the console never catches
up. `ufsdiagquiet=1` is required for any boot that is meant to reach userspace.

Separately, the kernel console and SpringBoard share one framebuffer: every
kernel line repaints over the UI. `sweetfb_noconsole=1`
(`iokit/Kernel/arm/SweetFramebuffer.cpp:320`) stops console drawing from the
first swap onward; output still reaches the persistent log and panics still draw.

## Known-open

- `sweet_sandbox_priv_grant` (`bsd/kern/sweet_amfi.c`) grants only
  `PRIV_WORK_INTERVAL`; everything else returns `EPERM` and logs
  `SWEET Sandbox: <proc> privilege N denied`. Seen for `healthd` (1002),
  `logd` (1002), `identityservicesd` (12000).
- `__nexus_register` repeatedly fails for `identityservicesd`.
- `bluetoothd` and `fairplayd.H2` restart in a loop.
- RTC not read; the clock shows December 31.
- No GPU driver. backboardd polls `IOAcceleratorES` and finds nothing; UI runs
  on CoreAnimation's software renderer.

## GPU path

Reference implementation exists: `joranv1/ios-sailfish` — iOS 16.7.16 on
Snapdragon 821 / Adreno 530 with GPU-accelerated UI. Approach:

- kernel: `bsd/dev/sweet_gpu.c` + `iokit/Kernel/arm/SweetGPU.cpp` — an Adreno
  driver (power, clocks, SMMU, CP microcode, zap shader) exposing an msm-style
  buffer/submit interface. No Apple AGX.
- userland: Mesa freedreno built as `/usr/lib/sweetgl.dylib` with an EAGL shim,
  injected into backboardd via
  `sweetenv=backboardd:DYLD_INSERT_LIBRARIES=/usr/lib/sweetgl.dylib`.
- No Metal; CoreAnimation falls back to its OpenGL ES renderer.

That work is ~3,900 added lines across ~30 patches on a530.

For a650/SM8250 the hardware facts are no longer unknown — they were measured on
this K40. See `alioth-gpu-a650-facts.md`. Short version: steps 1–19 of the Adreno
bring-up pass on real hardware (GMU firmware, HFI, 3D core at 525 MHz, SMMU
stream mask `0x401`, and the zap shader authenticated and started by TrustZone).
Step 20 — writing `CP_SQE_CNTL=1` to start the command processor — resets the
whole phone, and a long list of causes has already been excluded. That is the
ceiling today.

Our kernel has no GPU driver at all: no `adreno`, no `SweetGPU`, no
`RedwoodGPU`. Adding one is a port of `redwood_adreno.c`, not a configuration
change.
