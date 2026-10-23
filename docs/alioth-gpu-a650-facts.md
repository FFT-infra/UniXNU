# Adreno 650 on alioth — measured facts

Everything here was measured on this K40 (SM8250-AC), not derived from Linux
sources. Primary source: `_ref/ios-alioth/`, specifically
`src/hackios-xnu-kernel-main/xnu/files/bsd/dev/redwood_adreno.c` (4570 lines,
`struct adreno_soc soc_a650` at line 257) and `docs/SESSION-HANDOFF.md`
(§9, §9.5, and the step table at line 539).

Two separate projects, two incompatible ABIs. Do not mix them.

## Register and clock facts

| Item | Value | Note |
|:--|:--|:--|
| GPU register blocks | `0x3d00000` `0x3d6a000` `0x3d90000` `0x3da0000` `0x3de0000` | same as redwood (a660) |
| `chip_id` | `0x06050002` | Adreno 650 |
| GMEM | 1 MiB + 128 KiB | |
| UBWC highest bank bit | 16 | LPDDR5 |
| SMMU `idr0` / `idr1` | `0x4c017e06` / `0x20000005` | 6 SMRs, 5 contexts |
| **stream mask** | **`0x401`** | not `0x400`. Writing `0x400` makes SMR read back 0 |
| `counter_xo_div4` | `0x4300c` | not `0x6200c` (`debugcc-kona.c`) |
| `counter_gcc_gpucc` | `0x163` | not `0x146` |
| GPUCC post-div field width | 2 bits | mask `0x3`, not `0xf` |
| 525 MHz | RPMh level 5 | measured 524999528 Hz |

## Firmware

All in `/vendor/firmware` (not `firmware_mnt/image`):

| File | Size | Note |
|:--|:--|:--|
| `a650_gmu.bin` | 41548 | |
| `a650_sqe.fw` | 31988 | version word at +4, low 12 bits `0x114` |
| `a650_zap.mdt` | `0x1acc` | two pages |
| `a650_zap.b02` | `0x68c` | |

## Zap shader: the question is answered

The open question was whether SM8250's TrustZone lets a non-Android kernel load
the zap shader. It does. Step 19 on real hardware: `give-up`, the signature
check, and `SCM_PAS_MEM_SETUP` all return 0, then
`TRUSTZONE HAS AUTHENTICATED AND STARTED THE ZAP SHADER`.

One non-obvious consequence: the address in Mu-Silicium's `alioth.dtb`
(`pil_gpu_region` at `0x8681a000`) is **below** the physical window the loader
hands this kernel (`0xf4000000` and up). Reading it stops the processor — no
translation, or the bootloader's XPU still owns it. Hence `soc_a650.zap_memory = 0`
and the driver takes two contiguous pages of its own via `kmem_alloc_contig()`
plus `kvtophys()`.

## Step 20 is the wall

Writing `CP_SQE_CNTL = 1` produces no further kernel output and the whole phone
resets within ~1 s. Everything before that write is fine
(`cp_apriv_cntl 0x6e`, `rbbm_status 0x1`).

Excluded on real hardware, each with a control:

- microcode address (`adreno_blind=1`)
- zap ordering (`adreno_zap_first=1`; also skipping step 19)
- SMMU refusing the first fetch (`adreno_direct=1` bypass)
- register offsets (matched line by line against the vendor `a6xx_reg.h`)
- SCTLR / PTE attributes
- idle power collapse (2.5–3 s idle after core programming, no reset)
- `SPTPRAC` (the control register will not take a write)
- `GMU_OOB_GPU_SET` (neither interrupt bit acknowledged)
- `CP_RB_RPTR_ADDR` and its bit 27 (`adreno_rptr=1`)

The decisive control: **writing 0 to the same register is harmless**. What kills
it is starting the command processor, not the address or the value.

What remains is the back half of the vendor `a6xx_start()`: `RBBM_CLOCK_*`,
VBIF, the a650 TPL1 weights, `CP_PROTECT`, GMU counters.

Usable posture: `adreno_return=1` with `last_step=19` — leave the driver at
"power on, clocks running, SMMU up, zap loaded", then return to the loader.
That is the ceiling reachable today.

## The two ABIs

### ios-sailfish (`/tmp/probe-gpu/ios-sailfish`) — what `sweetgl` speaks

`tools/sweetgl/sweet_drm.c` is not a DRM ioctl client. Every call is
`IOConnectCallScalarMethod(conn, selector)` against `IOServiceMatching("SweetGPU")`,
selectors 10–16. The DRM layer is faked in userland; the "fd" is a `dup()` of
`/dev/null` (line 75, 85). Kernel side is
`iokit/Kernel/arm/SweetGPU.cpp` + `bsd/dev/sweet_gpu.c`.

### ios-alioth — `RedwoodGPU`

`iokit/Kernel/RedwoodGPU.cpp`, an IOService + IOUserClient with Linux-uapi
command numbers:

```
kMsmGemNew    0x42    kMsmGemInfo    0x43    kMsmGemCpuPrep 0x44
kMsmGemCpuFini 0x45   kMsmGemSubmit  0x46    kMsmGemMadvise 0x48
```

This one is closer to Mesa's msm backend. But `RedwoodGPU` will not `start()`
unless `redwood_gpu_ready()` returns true — i.e. until step 20 succeeds. Until
then the service is permanently empty.

**So a `sweetgl` port can be built, installed and made to fail cleanly. It cannot
connect to anything, and it is not worth changing the kernel for.**

## Porting cost to this tree

`redwood_adreno.c` is built against xnu 8796.121.2 with 4 KiB pages; ours is
8796.141.3 with 16 KiB pages. The public APIs it uses all exist here
(`ml_io_map`, `kvtophys`, `kmem_alloc_contig`, `PE_parse_boot_argn`). Its only
private header is `redwood_gpu_layout.h`. The real rework is the ADT constant
extraction and the `PAGE_SIZE`-dependent allocations.

## Do not use

`UniXNU/out/alioth/ios-sweet-0081/diag-markers-v25/` — built on the retracted
"timer is dead, `delay(1)` never returns" premise. Do not compile it, do not
boot it.
