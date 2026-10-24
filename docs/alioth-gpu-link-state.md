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

Four boots on the working ADT, all read, all clean.

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

| boot | image | kernel | what it showed |
|:--|:--|:--|:--|
| 16:37 | v40b-sample600 | v40 | reached iOS (the pre-ADT-discovery baseline) |
| 17:37 | v41d-gpu20 | v41d | **never booted** — broken ADT, ring untouched |
| 19:16 | v41e-ctl120 | v41e | **never booted** — broken ADT, ring untouched |
| 21:19 | v40-ctl120-adt | v40 | **reached iOS**; no `adreno:` lines — the driver is not in v40 |
| 21:26 | v41f-ctl120-adt | v41f | **reached iOS**; no `adreno:` lines — objects in, driver never called |
| 21:29 | v41e-ctl120-adt | v41e | **reached iOS**; `adreno: SoC a660 (an Adreno 642L)` twice |
| 21:33 | v41e-gpu120-adt | v41e | **reached iOS**; `adreno: SoC a650 (an Adreno 650)`, then the SoC-base guard |

The control is settled. v41f differs from v40 only by five linked objects and
boots identically — no constructor kills anything. v41e differs from v41f only
by `sweet_ufs.o` calling the driver, and prints `soc_select()`'s line, which
runs before any boot-arg test. So the call is safe too, and the objects are in.

`v41e-gpu` is the first boot where `gpu=1` took effect, and it stopped at
`start_by_itself()`:

```
adreno: THE GPU IS NOT STARTED: gpu=1 is for the phone, and this is not it
```

`start_by_itself()` compares `pe_arm_get_soc_base_phys()` against
`REDWOOD_SOC_BASE = 0x00400000`. That function
(`pexpert/arm/pe_identify_machine.c:178`) reads `/arm-io`'s `ranges` and returns
`*(ranges + 1)` — the second 64-bit word. This ADT's `ranges` is
`u64(0, SOC_BASE, 0x100000000 - SOC_BASE)` with `SOC_BASE = 0x01000000`, so the
second word is `0x01000000`, not `0x00400000`. The guard is comparing against
the reference project's SoC base, not this tree's.

That is a one-constant mismatch, not a hardware result. Nothing has yet read a
GPU register on this phone.

## Every image since 18:00 was built on a broken ADT

The five GPU objects were never the reason those boots stopped at `AL38a
mbWaitCpus`. The ADT was.

Every builder in `diag-markers-v41` takes its ADT from the pinned source image
`alioth-ios16-diag.img`, whose ADT is `ff9cc8281ccb…`. The image that reached iOS
at 16:37 — `v40b-sample600` — carries a different ADT, `f819e0ff65fe…`. Both are
12800 bytes and differ in **exactly one byte, at offset 8800**: `0x1b` (27)
versus `0x13` (19). That byte is the length of the `reg` property of the GIC node
(`name = gic`, `compatible = arm,gic-v3`, `interrupt-controller`,
`virtual-timer-intid`). 27 bytes is three base/size pairs; 19 is two and a half.

| ADT | sha256 | GIC `reg` | used by |
|:--|:--|:--|:--|
| working | `f819e0ff65fe20a5…` | 0x1b | v22 … v40b-sample600 |
| broken | `ff9cc8281ccb5892…` | 0x13 | v1 … v21, and every v40-ctl120 / -dbg / v41d / v41e / v41f image |

The good ADT is on disk: `diag-markers-v41/adt-alioth-vtimer27.adt`, byte-identical
in every `diag-markers-v22` … `v40` directory. The builders already accept it:

```
--adt diag-markers-v41/adt-alioth-vtimer27.adt \
--adt-sha256 f819e0ff65fe20a5e61c0f5afd3bfb299f4c784d881a038b9606240cc6a73a03
```

The truncated GIC range plausibly breaks `SweetCPU::startAll`'s SGI path, which is
what must reach `ml_set_max_cpus()` before `mbinit`'s `ml_wait_max_cpus()` returns —
the exact spin the screen was showing.

Rebuilt with the good ADT, `v40-ctl120-adt` booted into iOS at 21:19 (`cpus=1`,
SpringBoard and backboardd running, `120 s elapsed, resetting`). That is the first
boot past `AL38a` since 16:37, and it settles the SMP question: **`cpus=1` was never
the problem.** The earlier `cpus=8` failures were the same bad ADT, not SMP.

## What the ring can and cannot show

The ring is written by `sweet_log_thread`, started by `sweet_log_start()` at
`sweet_ufs.c:667` with `sweet_log_secs` from `ufslogsecs` (default 10). A boot
that dies before that line writes nothing at all. With `ufslogsecs=1` the ring
is at most a second behind wherever the kernel stops.

That is what the four ADT boots show: each wrote a full iOS run, so each reached
`sweet_ufs_init()`. The 17:37 and 19:16 boots wrote nothing, so they died before
it — and the reason was the ADT, not the GPU.

The `-dbg` images (`cpus=8 ufslogsecs=1`) were built before the ADT was found
and still carry the broken one. They need rebuilding with `--adt` before they
mean anything. `cpus=8` itself is no longer suspect: `cpus=1` boots fine, and
the earlier `cpus=8` failures were the same ADT.

## The next variable

`REDWOOD_SOC_BASE` in `redwood_adreno.c:4649` is `0x00400000`; this tree's ADT
puts the SoC at `0x01000000` (`make-alioth-ios-adt.py:31`). One of the two is
wrong for this phone, and the guard is what stands between `gpu=1` and the
register sequence. Deciding which means reading `/arm-io` `ranges` out of
20H392's own device tree and comparing — not guessing.

Until that is settled, steps 1-21 have never run on this phone.

## 22:07 — the guard is passed, and the first SMC hangs

`REDWOOD_SOC_BASE` is now `0x01000000`. The proof is in the ADT, not in a
guess: the GIC's `reg` in this tree's ADT is relative to that base, and
`0x01000000 + 0x16a00000 = 0x17a00000`, which is SM8250's GICD — the address
the kernel is demonstrably taking its interrupts from. `0x00400000` would put
the GICD at `0x16e00000`, which is nothing. QEMU's own ADT puts `arm-io` at
`0x17000000`, so the guard still separates the phone from the test rig.

Disassembly of the rebuilt object confirms the change took:
`cmp x0, #0x400, lsl #12` became `mov w8, #0x1000000; cmp x0, w8`.

`v41g-gpu120-adt` (booted 22:07:43) then ran steps 1 through 18 and stopped at
19:

| step | what it showed |
|:--|:--|
| 17 | `THE 3D CORE HAS POWER AND ANSWERS`, 3D core clock 525 MHz, `gpu_rbbm_status = 0x1` |
| 18 | SMMU context 0 built and read back; `a650_sqe.fw`, 31632 bytes, into the ring; three memory windows |
| 19 | `about to ask TrustZone to give up the zap shader: smc 0x42000206, arguments 0xd, 0, 0` — **never returns** |

That is the first register sequence this phone has ever executed, and the
first time the driver reached TrustZone.

### Why step 19 hangs, and what it is not

`0x42000206` is `SCM_64 | SCM_SIP | SCM_PAS_SHUTDOWN` on image 13. `scm()`
issues it as `smc #0` with `x0..x7`, and the CPU does not come back.

The cpus=8 photograph proves the rest of the system kept running while cpu0
sat in the trap: `SWEET TOUCH` advanced 125 → 1250 and `SMP --- sample 0`
printed. So this is a hard SMC hang, not a GPU register read that faulted.

Two things were initially suspected and are now ruled out:

- **Not a PAS ordering problem.** The `gpu=1` path sets
  `skip_steps = 1<<10 | 1<<11 | 1<<12`, which skips steps 10, 11 and 12. Step
  11 is `step_trustzone` and step 12 is `step_zap_signature` — so this kernel
  has issued **no** PAS call before this one. There is no earlier
  `PAS_INIT_IMAGE` on image 13 for `PAS_SHUTDOWN` to conflict with.
- **Not the ADT.** Steps 1-18 read real registers through the same ADT and got
  real answers back.

What is left is that the SCM channel itself has never been exercised by this
kernel. `step_trustzone` is the only place that asks
`SCM_IS_CALL_AVAILABLE` first, and `gpu=1` skips it. The `0x42000206` call is
therefore the first SMC this kernel ever makes, on an untested channel.

### The log had to be forced out

`cpus=1` plus `pause_ms=0` means the GPU block never sleeps, so
`sweet_log_thread` never gets scheduled and the ring is never copied to the
medium. The screen was the only record of the 22:07 run.

`scm()` now calls `sweet_log_flush()` immediately before the `smc #0`
(`redwood_adreno.c`, declared `extern int sweet_log_flush(void)` at the top).
Verified in the object: `bl _sweet_log_flush` at `0x5fbc`, `smc #0` at
`0x5fe0`. `sweet_log_flush()` is the same entry point `sweet_autoreset.c:135`
uses before it resets the phone; it takes `sweet_ufs_lock` and writes through
the bounce buffer, so it is safe from thread context.

That produced `kernel.pie.v41h` (`868a331174205335`) and
`v41h-gpu120-adt.img`. It was booted at 22:43:41 and did not come back on USB
— expected, since `cpus=1` also keeps the autoreset thread off the CPU.

### The next variable

Not the zap order. `v41h-scm11-adt.img` runs **only** step 11
(`adreno=11 adreno_only=11`): the four `SCM_IS_CALL_AVAILABLE` probes and the
`PAS_IS_SUPPORTED` matrix, and nothing else. `step_trustzone` has no
precondition on GPU state, so it is the cheapest test of whether this kernel
can make an SMC at all.

- If it hangs on the first `SCM_IS_CALL_AVAILABLE`, the channel is the
  problem, not the GPU, and every PAS path is blocked behind it.
- If it returns, the channel works and `PAS_SHUTDOWN` on image 13 is
  specifically what the secure world refuses to answer — which points at
  `SCM_FAST` (Linux's `qcom_scm_pas_shutdown` does *not* set it, and
  `zap_unload()` does not either) or at the image id.

## The channel is fine; two PAS calls are not

`v41h-scm11-adt.img` (`adreno=11 adreno_only=11`) ran step 11 alone at 23:32:58.
It made **fifteen** SMC calls and every one returned:

| call | function id | result |
|:--|:--|:--|
| is-call-available | `0xc2000601` | 0, answer `0x1` |
| has init_image | `0xc2000601` | 0, answer `0x1` |
| has auth_and_reset | `0xc2000601` | 0, answer `0x1` |
| audio, both ways | `0xc2000207` / `0x42000207` | 0, 0 |
| modem, both ways | `0xc2000207` / `0x42000207` | **-3145792** (`0xffd00000`), 0 |
| video, both ways | `0xc2000207` / `0x42000207` | 0, 0 |
| GPU, both ways | `0xc2000207` / `0x42000207` | 0, 0 |

`"supported", asked at once / as Linux asks: audio 0/0, modem 0/0, video 0/0,
GPU 0/0`, then `step 11: done` and `alive 1..18 s after step 11`. So the channel
works, the non-FAST 64-bit form works, and the image id is not rejected
outright. The hang is specific to particular PAS commands.

`v41i` then removed `zap_unload()` from the path entirely — `zap_unload()` is
now behind `adreno_zapunload=1`, because `PAS_SHUTDOWN` (`0x42000206`) wedged
cpu0 twice and Linux's zap path never asks it anyway (`zap_shader_load_mdt` is
`qcom_mdt_load` → `pas_auth_and_reset`, nothing else). `kernel.pie.v41i`
(`2e0c0262e3311533`), booted 23:42:38:

```
adreno: 19: the zap shader goes into two pages of this kernel's own at 0x1043f0000
adreno: 19: 6860 bytes of header in 16384 pages at physical 0x104268000
adreno: 19: about to ask TrustZone to take the zap shader's signature: smc 0x42000201, arguments 0xd, 0x104268000, 0x1acc
adreno: 19: TrustZone returns 0, answer 0
adreno: 19: about to ask TrustZone where the image will be: smc 0x42000202, arguments 0xd, 0x1043f0000, 0x2000
adreno: 19: TrustZone returns 0, answer 0
adreno: 19: the reserved memory begins 00 00 00 00, the zap shader 10 d0 6d 01
adreno: 19: about to write 1676 bytes at 0x1043f0000
adreno: 19: about to ask TrustZone to authenticate the zap shader and run it: smc 0x42000205, arguments 0xd, 0, 0
```

`PAS_INIT_IMAGE` and `PAS_MEM_SETUP` both return 0/0. `PAS_AUTH_AND_RESET`
never returns. Two of the three PAS calls hang, and only on this phone.

### The firmware is not the problem

Every file in `out/alioth/fw-gpu/` is byte-identical to the phone's own
`vendor_a` copy, pulled through recovery and compared by sha256:
`a650_zap.mdt` `e768fe579a4ac738`, `a650_zap.b02` `a415e5452fa8f597`,
`a650_sqe.fw` `b4c27420fd6a5c6b`, `a650_gmu.bin` `df79fa8482fcc758`.

`a650_zap.mdt` is 6860 bytes and its bytes from `0x1000` on are `0xff`. That is
not truncation: it is `a650_zap.b00` (148) followed by `a650_zap.b01` (6712),
which is exactly the metadata `qcom_mdt_pas_init` assembles, and
`zap_header()`'s own check (`148 + 6712 == 6860`) agrees. The full image is
`a650_zap.elf`, 13964 bytes, which is `0x368c` rounded up — the same file the
phone ships.

`PAS_INIT_IMAGE` accepting it (answer 0) is the corroboration: the secure
world read the header and did not object.

### What is left: where the two pages are

`pipa` refuses exactly the arrangement this driver uses. Its `zapLoad` opens
with

```cpp
// The region must not be memory XNU hands out: TrustZone locks it after authentication.
if (kZapPhys + kZapBytes > vm_first_phys && kZapPhys < vm_last_phys) return stop(-20);
```

and takes `kZapPhys = 0x55615000` from the device tree's `no-map` region. This
driver's a650 row has `zap_memory = 0`, so `step_zap_load()` calls
`kmem_alloc_contig()` and gets `0x1043f0000` — inside the range the kernel
manages.

That range is now known exactly. `chosen`'s `dram-base` is `0x100000000` and
`dram-size` is `0x00000000fcb00000`, so XNU owns
`0x100000000 .. 0x1fcb00000`. `0x1043f0000` is inside it; the device tree's
`pil_gpu_region` (`0x8681a000`, 2 pages, `no-map`) is below it and outside.

The facts file says reading `0x8681a000` stops the processor. That was about
the CPU reading it; `PAS_MEM_SETUP` returning 0 shows TrustZone takes the
address. `step_zap_load()` does read the first four bytes before writing
(`about to look at the memory at`), so a CPU-side block would show up there,
one line before the write, and would not be confused with an AUTH hang.

`adreno_zap_mem=ADDR` already exists (`redwood_adreno.c:315`) and takes the
device tree's address without any code change — so the next boot is one boot
argument, not a rebuild.

### Still to reconcile

`alioth-gpu-a650-facts.md:38-42` records step 19 succeeding on real hardware —
`give-up`, the signature check and `SCM_PAS_MEM_SETUP` all returning 0, then
`TRUSTZONE HAS AUTHENTICATED AND STARTED THE ZAP SHADER`. Today `give-up` and
`auth_and_reset` both wedge cpu0. One of the two records is wrong, and the
difference between the two runs is what would explain the hang.

