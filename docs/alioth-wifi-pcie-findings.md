# alioth WiFi: what the PCIe controller looks like at boot

The Wi-Fi on this phone is a QCA6390 (`17cb:1101`) behind `qcom,pcie@1c00000`,
not the AHB WCN6750 the tree's `redwood_wifi.c` was written for. The port so far
is the controller bring-up only: `bsd/dev/alioth_pcie.c`, taken from
`_ref/ios-alioth` (the Redmi K20 Pro / alioth worktree) and adapted.

## What was adapted

The reference file logs through `loader_return.c`'s `loader_log_line`, which
this tree does not have. `printf` already appends to the sweetlog ring
(`osfmk/console/serial_console.c` `_cnputs` -> `sweet_log_append`), so the log
layer became `printf` plus an explicit `sweet_log_flush()` before each register
access (`risky()`). The flush is what makes the last line survive an access
that never returns.

Three further changes:

- `map_all()` is now idempotent. With `pcie_thread=1` the reference called it
  twice, mapping the whole window a second time out of `io_submap`.
- The `gcc` window went from 2 MiB to 1 MiB. The highest offset read is
  `0x9000c`.
- The `conf` window went from 1 MiB to 4 KiB. Only bus 0, device 0, function 0
  is read (`0x00`, `0x0c`).

The reason is `io_submap`: `osfmk/arm/io_map.c` sizes it `IO_MAP_SIZE =
(8ul << 20)`, and `io_map()` allocates from it with `KMA_NOFAIL`. A mapping
that does not fit panics in `kmem_alloc` with `KERN_NO_SPACE` (the v34 panic on
2026-10-24). Budget with the windows as they now stand:

| driver | mapped |
|:--|--:|
| keys | 0.094 MiB |
| spmi | 0.156 MiB |
| rpmh | 0.062 MiB |
| touch | 1.516 MiB |
| usb | 0.656 MiB |
| ufs | 0.062 MiB |
| autoreset | 0.031 MiB |
| **pcie** | **1.094 MiB** |
| GPU (ported, not yet linked) | 1.437 MiB |
| **total** | **5.108 MiB of 8.000** |

Before the window shrink pcie alone was 3.078 MiB and the GPU would not have
fit beside it.

## Step 1 on the device (image v39, 2026-10-24)

`pcie=1` performs reads only. Boot reached SpringBoard; the log's last lines
are `exec fairplayd.H2`.

```
pcie: 0: the Wi-Fi's controller at 0x1c00000, steps up to 1, without those in mask 0
pcie: 0: the blocks are mapped
pcie: 1: gcc pcie_0_gdscr (0xfffffe5477fa3004) = 0x22f001
pcie: 1: gcc pcie_0_axi_misc (0xfffffe5477f8a008) = 0x4104340
pcie: 1: gcc pcie_0_slv_axi_halt = 0x80000220
pcie: 1: gcc pcie_0_mstr_axi_halt = 0x80000220
pcie: 1: gcc pcie_0_cfg_ahb_halt = 0x80000000
pcie: 1: gcc pcie_0_aux_halt = 0x80000000
pcie: 1: gcc pcie_0_pipe_halt = 0x80000000
pcie: 1: gcc pcie_0_slv_q2a_halt = 0x80000000
pcie: 1: gcc pcie_phy_aux = 0x80000000
pcie: 1: gcc pcie0_phy_refgen = 0x1
pcie: 1: gcc aggre_noc_pcie_tbu = 0x4221
pcie: 1: gcc pcie_0_bcr (core reset) = 0
pcie: 1: gcc pcie_0_phy_bcr (phy reset) = 0
pcie: 1: the controller is off (gdscr 0x22f001), its clock votes 0x4104340
pcie: 1: its clocks are not all voted for, so neither its PHY's nor its own
           registers are read here
pcie: 1: step 1: done
```

What the values say:

- `gdscr 0x22f001`: bit 31 (`PWR_ON`) is clear and bit 0 (`SW_COLLAPSE`) is
  set. The controller is **off**, not half-on. The bits that are set are
  `EN_REST_WAIT`, `EN_FEW_WAIT`, `CLAMP_IO`, `RETAIN_FF_ENABLE` -- the reset
  state, not a running one.
- `axi_misc 0x4104340`: bits 0-5 are `0x0`, so **none of the five clock votes
  is in**. The reference note that steps 1-4 "pass on hardware" is true, but
  the starting point is fully collapsed, not left on by Android.
- `phy_aux 0x80000000` and `pcie0_phy_refgen 0x1`: the PHY's branch and the
  reference generator's branch are both off at the branch level.
- Both resets read 0, which is their released state.

So step 2 (`gdscr &= ~SW_COLLAPSE`) is the first write, and it is a write to a
collapsed power domain rather than a nudge to one that is already up.

## Still missing for WiFi

The controller bring-up is not the driver. Between a trained link and a working
interface there are three layers this tree has no implementation of, in either
the local reference or upstream `UniXNU/NativeWifi`:

1. MHI: the QCA6390's host interface, channels, doorbells.
2. QMI: the board parameters the firmware asks for over that channel.
3. ath11k: `QCA6390/hw2.0/board-2.bin` and `firmware-2.bin`, and the WLAN
   device that results.

`UniXNU/NativeWifi` (HEAD `dee6ec5c`, 2026-10-23) is ath12k: WCN7850
(`17cb:1107`) and QCN9274 (`17cb:1109`). It is a different generation with a
different MHI layout, so nothing of its driver body carries over. What does
carry is the board contract (`struct nw_platform`, `struct nw_board` in
`include/nw_abi.h`) and its PCIe probe, and for both of those the local
reference is closer to this phone.

## GPU is a separate port, and it is further along

`redwood_adreno.c` carries a `soc_a650` row commented
`/* the Redmi K40 (alioth), SM8250-AC */`. On this phone the reference has run
it to step 19: GMU firmware live (`core 0x20010008`, `HFI 0x20000001`), 3D core
powered, 3D clock measured `524999528 Hz`, TrustZone authenticated and started
the zap shader. Step 20 (`CP_SQE_CNTL=1`) resets the whole phone; a control
experiment showed the same register written 0 is harmless, so it is the start
action itself.

The gap here is not the driver, it is six symbols this tree does not define:
`loader_log_line`, `loader_log_putc`, `loader_return` (`loader_return.c`),
`redwood_image_file` (`redwood_image.c`), `MiniIOMobileFramebufferShow`
(`MiniIOMobileFramebuffer.cpp`), and `redwood_tl` (`redwood_input.c`). The three
GPU objects otherwise compile clean and reference nothing else the v36 kernel
lacks.

## Steps 1-4 on the device (image v39, pcie=4, 2026-10-24)

Autoreset at 120 s returned the phone to fastboot, so nothing in 2-4 hangs.

```
pcie: 2: about to write gcc pcie_0_gdscr at ...: 0x22f000
pcie: 2: gcc pcie_0_gdscr after = 0xf822f000
pcie: 2: THE CONTROLLER HAS POWER (gdscr 0xf822f000, 1 ms)
pcie: 2: step 2: done
pcie: 3: gcc pcie_0_axi_misc after = 0x410436f
pcie: 3: gcc pcie_0_slv_axi_halt  = 0x220      (was 0x80000220)
pcie: 3: gcc pcie_0_mstr_axi_halt = 0x220      (was 0x80000220)
pcie: 3: gcc pcie_0_cfg_ahb_halt  = 0          (was 0x80000000)
pcie: 3: gcc pcie_0_aux_halt      = 0          (was 0x80000000)
pcie: 3: gcc pcie_0_slv_q2a_halt  = 0          (was 0x80000000)
pcie: 3: step 3: done
pcie: 4: gcc pcie_phy_aux after = 0x1
pcie: 4: phy phy_power_down = 0
pcie: 4: phy phy_status = 0x68
pcie: 4: the PHY IS READY (status 0x68, bit 6 is its ready)
pcie: 4: step 4: done
```

Three facts that differ from the reference notes:

- The power domain comes up from a collapsed state in **1 ms**. The reference
  recorded step 4 passing with `cx_gdscr` written `0x222000` and read back
  `0xf8222000`, which is the same shape of transition.
- All four halt bits clear as soon as the votes go in. The two AXI branches
  settle at `0x220` rather than `0`, which is their own status field, not a
  halt.
- `phy_status` is **already `0x68`**, bit 6 set, before anything is written to
  the PHY. The bootloader left the PHY initialised. The reference's
  "PHY_STATUS bit 6 never sets" does not hold on this phone.

That last fact makes step 5 (the whole PHY sequence, plus a pulse of
`pcie_0_phy_bcr`, `pcie_phy_bcr` and a release of `pcie_phy_com_bcr`) both
unnecessary and the most dangerous step in the table: the reference's own
comment says releasing the common PHY reset after the PHY's own stopped the
machine. Steps 6-8 are read-only clock dumps, the aux clock source fix, and the
PARF read that used to hang. `pcie=8 pcie_skip=32` (bit 5) takes those three
without touching the PHY.

## Step 9 on the device (kernel v40, pcie=9 pcie_skip=32, 2026-10-24)

Step 9 is the first time anything is written into PARF. Every write was
accepted:

```
pcie: 9: gcc pcie_0_bcr after = 0
pcie: 9: parf parf_device_type before = 0
pcie: 9: parf parf_device_type after  = 0x4
pcie: 9: gcc phy_refgen_src rcgr after = 0x12
pcie: 9: gcc phy_refgen_src cfg  after = 0x501
pcie: 9: gcc pcie0_phy_refgen    after = 0x1
pcie: 9: parf parf_phy_ctrl = 0x900
pcie: 9: parf parf_sys_ctrl  = 0x2000361c -> 0x365e
pcie: 9: parf parf_slv_addr_space_size after = 0x4000000
pcie: 9: gcc ddrss_pcie_sf_tbu after = 0x100001
pcie: 9: parf parf_mhi_clock_reset_ctrl = 0x3 -> 0x13
pcie: 9: parf parf_axi_mstr_wr_addr_halt_v2 = 0x80000014
pcie: 9: step 9: done
```

Notes:

- `parf_device_type` reads 0 and takes 4. The controller was not configured as
  a root complex by the bootloader; that is now done.
- `slv_addr_space_size` at `0x358` reads back what was written. The DT's
  `qcom,slv-addr-space-size` is `0x4000000` (64 MiB), and that is what is used
  now, not the reference's `0x10000000`. The `0x16c` register of the older IP
  version reads 0 on this SoC, which is consistent with `0x358` being the right
  offset here.
- `parf_sys_ctrl` goes `0x2000361c -> 0x365e`: bit 29 cleared, bit 6 (core clock
  gating off) and bit 1 set.
- `parf_mhi_clock_reset_ctrl` takes bit 4 (`0x3 -> 0x13`).
- `parf_axi_mstr_wr_addr_halt_v2` already had bit 31 set (`0x80000014`); the
  write changed nothing.

### A bug in the reference, found on the way

`step_look()` read `phy_status` out of block 1 and `parf_sys_ctrl` out of block
2. Block 1 is parf (`0x1c00000`) and block 2 is phy (`0x1c06000`), so both were
wrong. It never showed because step 1 returns before that point whenever the
clock votes are incomplete, which they always are at boot on this phone. Steps
4 and 8 address the two blocks correctly. The block indices in `step_look` are
fixed in this port.
