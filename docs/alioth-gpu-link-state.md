# The GPU driver has never been in the kernel this phone runs

Until today the three Adreno objects were not in any bootable image. What the
phone has been booting since v39 contains no `redwood_adreno.c`, no
`RedwoodGPU.cpp`, and no `adreno:` strings at all.

## The evidence

`objects.v39.response` — the object list behind `kernel.pie.v40.stripped`, which
is the kernel every boot since 15:11 has run — does not name
`redwood_adreno.o`, `RedwoodGPU.o` or `MiniIOSurface.o`. Neither does the
binary: `nm` finds no `redwood_adreno_init`, `nm -a` finds no `redwood_*` at
all, and `strings` finds 0 `adreno:`.

So the step-20 measurements recorded in `alioth-gpu-a650-facts.md` were made on
the reference project's kernel, not on an image built from this tree. That is
the gap the last several days of GPU work were sitting behind.

## Why the link failed when they were added back

Three separate blockers, in the order they appeared:

1. **Six undefined symbols.** `redwood_adreno.c`, `RedwoodGPU.cpp` and
   `MiniIOSurface.cpp` ask for `loader_log_putc`, `loader_log_line`,
   `loader_return`, `redwood_image_file`, `redwood_tl` and
   `MiniIOMobileFramebufferShow`. The reference gets these from
   `osfmk/arm64/loader_return.c`, `miscfs/apfs/redwood_image.c` and
   `iokit/Kernel/MiniIOMobileFramebuffer.cpp` — none of which exist here
   (this tree has no loader channel, no APFS, and its display driver is
   `iokit/Kernel/arm/SweetFramebuffer.cpp`, which publishes no C entry point).
   Supplied by `diag-markers-v41/src/bsd/dev/redwood_glue.c`, which logs
   through sweetlog, reads the four a650 blobs from
   `redwood_fw_a650.c` (generated, 248418 bytes), and shows a picture through
   `PE_current_console` + `ml_io_map` because `v_baseAddr` is physical.

2. **44 duplicate symbols.** `MiniIOSurface.cpp` is a second implementation of
   `IOCoreSurfaceRoot` and `IOSurfaceRootUserClient`. This tree already has
   both, in `iokit/Kernel/arm/SweetSurface.cpp`, and the window server is
   already drawing through them (`SWEET SURF:` lines in every log). Two of each
   class cannot link, and the one in use has to be the one the GPU draws into.
   So `MiniIOSurface.o` is out and `RedwoodGPU.cpp` was rewritten against
   `SweetSurface`: `sweet_surface_copy()` instead of `MiniIOSurfaceCopy()`,
   `release()` instead of `MiniIOSurfaceDrop()`, `s->width/height/bytesPerRow/
   pixelFormat/planeCount/offset/allocSize` instead of `s->info.*`, the pixels
   at `s->offset` rather than `PAGE_SIZE` (SweetSurface keeps its state page in
   a separate descriptor, not in front of the pixels), and the seed read from
   `s->state` at byte 0xc — the same word `SweetSurface.cpp`'s lock trap uses.

3. **Nothing called the driver.** `redwood_adreno_init()` had a definition and a
   declaration in `bsd/sys/redwood_input.h` and no caller anywhere in the tree.
   A linked-in driver that is never invoked is indistinguishable from one that
   is absent — which is exactly what the v39 object list would have produced
   even if the three objects had been there. Added at the end of
   `sweet_ufs_init()` in `bsd/dev/sweet_ufs.c`, after `sweet_usb_probe()` and
   beside `alioth_pcie_init()`. It reads its boot args (`gpu=1` or `adreno=N`)
   and returns without touching a register otherwise.

## What links now

| | objects | kernel | sha256 |
|:--|:--|:--|:--|
| v40 (running) | v39 list | `kernel.pie.v40.stripped` | `73ce91c1…` |
| v41e | + 5 GPU objects, `sweet_ufs.o` calls the driver | `kernel.pie.v41e.stripped` | `69e7c193…` |
| v41f | same, `sweet_ufs.o` from v39 (no call) | `kernel.pie.v41f.stripped` | `5fe7d0ba…` |

Both link clean. v41f is the control that isolates "the objects are in" from
"the driver runs"; v41e is the one that can do anything.

## The register sequence, against pipa

`xVanTuring/UniXNU-pipa` is the only published SM8250 + A650 project that got
past `CP_SQE_CNTL = 1`, and its CP start is Linux v6.6's `hw_init` +
`a6xx_cp_init` in the "A610, no APRIV" shape. Four things it does that this
driver does not, in the order they are likely to matter:

- **APRIV is half-set.** `redwood_adreno.c:3334` writes `CP_APRIV_CNTL = 0x6e`
  while `CP_RB_CNTL` keeps bit 27 (`CP_RB_CNTL_VALUE`, line 2735). pipa writes
  neither — no APRIV, bit 27 set — and Linux writes APRIV *and* clears bit 27.
  This driver is the only one of the three doing half of each. It is the best
  single explanation for "writing 0 is harmless, writing 1 kills the phone",
  because 0 never starts the SQE.
- **The ring is dirty.** Nothing writes `CP_RB_WPTR = 0`, `CP_RB_RPTR_ADDR` or
  `CP_SCRATCH(2) = 0` before `CP_SQE_CNTL`. pipa writes all three
  (`NativeAdrenoRing.cpp:212-216`). Whatever the last boot left in the ring is
  what the SQE starts fetching from.
- **The interrupt mask is off.** `RBBM_INT_0_MASK` is written only under
  `adreno_hwcg=1`; pipa writes `0x03d0e242` unconditionally, which is what makes
  a CP error visible instead of a silent reset.
- **`CP_PROTECT` is off.** Same gating. Not a trigger, but it removes the limit
  on what a mis-executing SQE can reach.

Also unshared: pipa writes `RB_DBG_ECO_CNTL` never, this driver writes
`0x04100000`; pipa hangs the SMMU on four streams (`0/1/0x400/0x401`), this one
on stream 0 only; pipa reads back `ucheRow()` between `CP_SQE_INSTR_BASE` and
`CP_SQE_CNTL`, which is read-only and changes nothing.

## What pipa does not solve

It reaches a running CP and a completed submission, then fails every UCHE read
with `RBBM_INT_0` bit 24 (`UCHE_OOB_ACCESS`), SMMU clean. Its own README lists
four untried experiments: no-zap + `RBBM_SECVID_TRUST_CNTL = 0`, `CP_APRIV_CNTL`
set the way Linux sets it, `GBIF_SCACHE_CNTL1`, and a GMU bandwidth vote. None
of those is in its code. So a working A650 Metal stack does not exist publicly;
the ceiling is a CP that runs and a GPU that refuses to read.

## Metal

There is no off-the-shelf drop-in. The route both pipa and sheng take is
`IOAcceleratorES` → `MetalPluginName` → a bundle under
`/System/Library/Extensions` → principal class `-initWithAcceleratorPort:`,
with a same-named `AGXAccelerator` imitation — never Apple's IOGPUFamily kext.
That means the sparse-prelink blocker (the `xnu-10063.141.1-sparse-index.patch`
exists only for xnu-10063; 8796.141.3 panics on a kext without
`LC_SEGMENT_SPLIT_INFO` in `kalloc_type_view_parse`) only blocks the
IOGPUFamily-prelinked route, not this one.

20H392's kernelcache does carry `IOGPUFamily`, `IOMobileGraphicsFamily`,
`IOSurface` and `AGXG10P`, and `AGXG10P_B0`'s `MetalPluginName` is
`AGXMetalA11` — whose bundle is present in the v16 root image (29 hits,
`NSPrincipalClass AGXPrincipalDevice`). `AGXMetalG10A0` (the `AGXG10P_A0`
personality) is not. Neither is reachable until the GPU itself answers, which
is what step 20 is about.

## Where this stands

One boot of the GPU objects has been read, and it says nothing about them.

The v41d log pulled at 18:17 is **not** from v41d. Its ring header is
`total 151574, flush #61`, byte-identical to the ring pulled at 19:14 with the
phone idle in between, and its last line is `600 s elapsed, resetting` — v41d
had `sweet-autoreset=600` and would have had to survive to that second to write
it. The ring it was read from is the `v40b-sample600` run from 16:37. A boot
that never reaches `sweet_ufs_init()` writes nothing: the ring, the autoreset
thread and the log flush are all started from inside that function.

`SWEET AUTORESET` powers the phone off rather than dropping it to fastboot, so
"no device on USB afterwards" is the designed end of every instrumented boot,
not a symptom. The `v40b-sample600` run at 16:37 ended the same way and was
healthy. It is not evidence either way.

| boot | image | kernel | cmdline | ring after |
|:--|:--|:--|:--|:--|
| 16:37 | v40b-sample600 | v40 | `sweet-autoreset=600` | written; ends `600 s elapsed, resetting` |
| 17:37 | v41d-gpu20 | v41d | `gpu=1` absent | **untouched** — still the 16:37 run |
| 19:16 | v41e-ctl120 | v41e | `gpu=1` absent, autoreset 120 s | **not read yet** — phone off, needs power-on |

The 19:16 run is the control: the driver is called but `gpu=1` is absent, so
`redwood_adreno_init()` runs `soc_select()` — which prints unconditionally —
and then returns at the boot-arg test. Its ring is the one thing that would
separate "the driver is inert without `gpu=1`" from "the driver kills the kernel
before it prints". That pull is the next step, and it costs one power-on.

v41d is not being read as evidence for a second reason too: it changed two
things at once — `sweet_ufs.o` calling `redwood_adreno_init()` **and**
`SweetSurface.cpo`/`SweetFramebuffer.cpo` rebuilt with clang 21 (every other
object in the link is clang 15). Both are backed out in v41e/v41f: the clang 15
`SweetSurface`/`SweetFramebuffer` are back in the response, and `RedwoodGPU.cpp`
was rewritten to need nothing from `SweetSurface.h` beyond what it already
exported. `kernel.pie.v41f` is that: the five GPU objects in, the driver never
called, nothing else changed.

The images built and waiting:

| image | kernel | what it tests |
|:--|:--|:--|
| `alioth-ios16-0081-markers-v41e-ctl120.img` | v41e | driver called, `gpu=1` absent — booted 19:16, ring unread |
| `alioth-ios16-0081-markers-v41f-ctl.img` | v41f | objects present, driver never called — must boot exactly like v40 |
| `alioth-ios16-0081-markers-v41e-gpu.img` | v41e | `adreno_soc=a650 gpu=1` — steps 1-21 and the first real measurement |
| `alioth-ios16-0081-markers-v40-dbg.img` | v40 | `cpus=8 ufslogsecs=1` — baseline for the instrumented cmdline |
| `alioth-ios16-0081-markers-v41f-dbg.img` | v41f | same cmdline, objects in, driver never called |
| `alioth-ios16-0081-markers-v41e-dbg.img` | v41e | same cmdline, driver called, `gpu=1` absent |

Order: pull the 19:16 ring first. If it shows `adreno: SoC ...`, the control is
clean and `v41e-gpu` can run. If it is untouched again, the next boot is
`v41f-ctl` — with the objects in but never called — which separates "one of the
five objects kills the kernel at constructor time" from "the call does".
`v40-dbg` is the sanity check on the instrumented cmdline and should be run
before any of the `-dbg` images is trusted.

## The cmdline has to change before any of this can be read

The ring is written by `sweet_log_thread`, started by `sweet_log_start()` at
`sweet_ufs.c:667` with `sweet_log_secs` from `ufslogsecs` (default 10). The
17:37 boot left the ring exactly as the previous run had it, so that boot died
inside its first 10 s — and `sweet_autoreset_start()` sits two lines later, so
a death in the driver leaves no reset armed: the cmdline has `cpus=1`, and a
wedged single CPU runs neither the reset timer nor the flush timer.

So the diagnostic images carry `cpus=8 ufslogsecs=1`: eight cores so a wedged
one does not stop the flush, one-second flushes so the ring is at most a
second behind wherever the kernel stops.

| image | kernel | cmdline |
|:--|:--|:--|
| `alioth-ios16-0081-markers-v40-dbg.img` | v40 | `cpus=8 ufslogsecs=1` — the baseline, must boot and return |
| `alioth-ios16-0081-markers-v41f-dbg.img` | v41f | same, objects in, driver never called |
| `alioth-ios16-0081-markers-v41e-dbg.img` | v41e | same, driver called, `gpu=1` absent |

Run them in that order. `v40-dbg` first: if the known-good kernel does not
come back under `cpus=8`, the SMP path is itself the variable and nothing else
in this table means anything.
