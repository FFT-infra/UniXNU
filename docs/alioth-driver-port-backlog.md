# alioth driver-port backlog

Date: 2026-10-23

Status: read-only audit complete. This document is an implementation backlog,
not evidence that any listed driver works on alioth.

Scope: drivers to evaluate after the ios-sweet-derived fast-boot proof. It
does not reopen UEFI, UniXNU, DarwinBoot, 4 KiB XNU entry, NativeUfs probe, or
the current root-mount path.

## 1. Evidence and safety rules

These rules apply to every milestone:

- A build, service registration, driver load, PSCI return code, or
  `fastboot OKAY` is not a functional pass. Acceptance needs the next-stage
  screen, log, probe readback, host-side enumeration, or user-visible result.
- Record the source commit, board/config commit, boot args, compiler, ADT/DT
  input, image hash, and verification result for every artifact.
- A milestone closes only after its success is reproduced from a cold boot.
- Change one variable per experiment. If the same failure occurs twice, stop
  and write down the eliminated hypotheses before changing more state.
- Every register-taking probe needs before/after controller and SMMU state.
  A "non-data-mutating probe" can still change UFS, SMMU, USB, PHY, or device
  state; do not call it absolutely read-only.
- Preserve a known-good recovery path. Do not infer slot state from GPT,
  manually change GPT attributes, or use `--set-active` as a diagnostic.
- Do not copy donor MMIO, GCC, PHY, PDC, APID, GPIO, SPI, panel, or SMMU
  constants. Recover alioth values from alioth DT/ADT and hardware readback.
- Do not treat firmware framebuffer output, console output, a mounted
  filesystem, an enumerated USB device, or injected HID events as proof of a
  different subsystem.

## 2. Audited source snapshots

Repository-relative files below are resolved against the named snapshot.

| Source | Audited ref | What is useful | What is not reusable as-is |
| --- | --- | --- | --- |
| `https://github.com/UniXNU/UniXNU` | `8a6672f7fa4ad6c41f9c5cca32992bc618752638` (`main`) | Driver boundaries, IOKit patterns, existing board-neutral drivers | No alioth USB, PMIC/button, display, FTS touch, or SMP board integration |
| `https://github.com/FFT-infra/UniXNU-core-alioth` | `bd78ae5a69e9f00459016f9cf27ed8798ff24d93` (`origin/main`) | Current core-fork integration point | This is a fork; it is not upstream `UniXNU/UniXNU` main |
| Local core tree | `fb042b900412a87b7caa28dea8e6857c861deb3a` (`feat/portable-tree-links`) | Local alioth integration state | Existing dirty files must be preserved |
| `https://github.com/UniXNU/NativeUfs` | `af785c1777161a62931425c2faaa536ce0bebd5b` (`main`) | UFS transport, GPT parser, SMMU takeover, root-window machinery | Generic ABI cannot take alioth writes for granted |
| Local NativeUfs | `6871d6467e6d98dfef407de2485438d121b0000b` (`feat/alioth-ufs-probe`) | alioth probe, stream verification, read-only root windows | Current dirty entries are only build/cache output; do not treat the local branch as upstream |
| `https://github.com/dipa-studio/ios-sweet` | `067c9b3d463ecfbe37520683637ea2e6af31f3a4` (`main`) | Darwin 22 ABI contracts and hardware-proven SM7150 implementation | All Sweet SM7150 hardware programming and layout constants |
| `https://github.com/UniXNU/UniXNU-sheng` | `9c88b90155ea5e400f5f75d06104dee96260619a` (`main`) | Provider/event architecture, HID contract, PMIC/button design patterns | SM8550 addresses, APIDs, PDC pins/SPIs, rails, clocks, GPIOs, and touch geometry |
| `https://github.com/Allenwdk/UniXNU-uke` | `0fde2b1b53b871e9b33b1b242a7034f56e2c71c4` (`main`) | UFS/root integration history and bring-up discipline | Board-specific SM6225 choices |
| `https://github.com/Allenwdk/UniXNU-uke` | `ce8498a06d6737d0558521a8635b8f5dfb417e11` (`uke-usbserial`) | DWC3 device-mode, descriptors, transfer rings, CDC-ACM structure | Board data, clocks, PHY values, SMMU stream `0x40`; only CI build/object audit is claimed |
| `https://github.com/wochgn/UniXNU-guacamole` | `6269d315dbf43c4f7334177721f5c18e9b633f97` (`main`) | ios-sweet HFS/`ufsroot0` integration on a different Qualcomm phone | Not an alioth hardware driver |
| `https://github.com/xVanTuring/UniXNU-pipa` | `6749f2fb443f81e64f76bf28406265b8e8c5d0e2` (`main`) | SM8250-adjacent configuration context | No general `drivers/` tree and explicitly not hardware-run |

The public upstream contains no alioth driver package and no drop-in
SM8250 USB, PMIC/button, display, touch, or PSCI implementation. This backlog
ports contracts and proven structures, then supplies alioth hardware data.

## 3. Locked alioth facts for implementation

| Area | Fact | Consequence |
| --- | --- | --- |
| UFS | Controller `0x1d84000`, compatible `qcom,ufshc`; streams `0x00e0` and `0x04e0` | Keep both streams. Never use `0x60` or `0x10` on alioth. Reuse an existing exclusive safe mapping before takeover |
| UFS probe | Probe 7 found exact exclusive bypass contexts; probe 10 reused them unchanged | This does not prove write DMA, cache ownership, or file-system safety |
| `userdata` | `first_lba=0x2a8000`, `last_lba=0x3b037fa`, `lba_bytes=4096`, capacity `242053263360`; block magic `d1da67fa521637a0`, F2FS magic `1127c75f` | Select by GPT name/GUID, never by an OS disk letter |
| Root architecture | NativeUfs currently publishes verified `SHNGROOT`/`OS`/`SystemOS` windows; the board instead selects whole-`userdata` journaled HFS+ via `rd=ufsroot0 ufsrw=1 rootrw=1` | These are two different storage contracts. Decide and document one; do not let both publish a root device |
| USB primary | `ssusb@a600000`, DWC3 core `0xa600000`, primary SMMU stream `0x00`, HS PHY `0x88e3000` | First target is warm device mode at high speed |
| USB secondary | `ssusb@a800000`, stream `0x20`; node disabled | Do not bring it up during the first USB milestone |
| USB SS PHY | QMP/SS PHY `0x88e8000`; node disabled | Defer; do not copy Sweet QSCRATCH/VBUS override writes |
| SMP | Eight CPUs, PSCI 1.0 over SMC, MPIDR `0x000` through `0x700`, GICv3 | Current ADT exports one CPU deliberately |
| PMIC | PM8150 family on SPMI SID 0; PON base `0x800` | Discover alioth APIDs/interrupt route; do not copy Sheng APIDs/PDC data |
| Buttons | Volume-up is PM8150 GPIO6, active-low. PON `KPDPWR` is power. `RESIN` is likely volume-down and must be verified | Test each button independently |
| Touch | FocalTech FTS, not Goodix, at `/soc/spi@990000/focaltech@0` | Goodix is the fingerprint device. Do not port Sweet's Goodix transport as touch |
| Touch transport | GENI SPI CS0 at 6 MHz, reset TLMM GPIO33, IRQ TLMM GPIO39, 10 contacts, raw axes `0x21c0 x 0x4b00` | `NativeGeniSpi` is only a controller; no FTS protocol driver exists yet |
| Display | GOP framebuffer `0x9c000000`, size `0x2300000`; K11A command-mode DSI, DSC, 1080x2400 | First target is CPU presentation through GOP/IOSurface/IOMobileFramebuffer. DSI takeover is later |

## 4. Ordered milestones

### M0: Freeze the ios-sweet fast-boot proof

Deliverable:

- A small manifest containing the exact working boot image, kernel, ADT/DT,
  root image, HFS/UFS layout, boot args, GPU/display mode, and recovery image.
- SHA-256 for every artifact and the exact source/config commits that
  produced them.
- The exact root-selection contract: `SHNGROOT` windows or whole-`userdata`
  HFS+; include volume UUID and writable/read-only state.
- The last observed UFS/SMMU state and the command or probe that produced it.

Pass evidence:

- A cold boot from the frozen artifacts again reaches the same first userland
  or panic boundary with materially identical logs and hashes.
- A documented recovery path has been checked without changing `boot_a`.

Rollback:

- Keep the frozen known-good image and recovery artifacts unchanged for every
  later milestone.

Exit gate:

- This manifest exists before any driver work. Do not embed experimental
  drivers into the baseline image.

### M1: Warm DWC3 device mode with CDC-ACM live logging

Why first:

- A persistent log channel reduces guesswork for SMP, buttons, display,
  touch, and storage. USB is not a prerequisite for root, so this milestone
  must not destabilize the frozen root path.

Primary donor:

- `Allenwdk/UniXNU-uke`, `uke-usbserial`,
  `ce8498a06d6737d0558521a8635b8f5dfb417e11`.

Files to study and port selectively:

- `drivers/NativeUsbSerial/UsbRing.h`
- `drivers/NativeUsbSerial/UsbDescriptors.h`
- `drivers/NativeUsbSerial/NativeUsbSerial.cpp`
- `drivers/NativeUsbSerial/UkeUsbDwc3.h`
- `drivers/NativeUsbSerial/UkeUsbPhy.h`
- `drivers/NativeUsbSerial/UkeUsbGcc.h`
- `drivers/NativeUsbSerial/tests/test_usb_serial.cpp`

Secondary contract references:

- ios-sweet `xnu/patches/0073-sweet-usb-device-ep0.patch` at
  `381274afe972a9cc24160fd61104aa521d722908`.
- ios-sweet `xnu/patches/0075-sweet-usb-ecm.patch` at
  `59e7aec292e3d269f3ef60d54cb70d01d555b641`.
- ios-sweet `xnu/patches/0077-sweet-usb-live-log.patch` at
  `5029e9119429d11dac84bf140558fbda85d37c8f`.
- ios-sweet `xnu/patches/0072-sweet-usb-probe.patch` at
  `809bf54c751f4e0e17c9a75a25cf0796a72abb9c` for the observe-before-write
  discipline only.

Exact first code slice:

1. Add `drivers/NativeUsbDevice/UsbRing.h` from Uke with no hardware access.
2. Add `drivers/NativeUsbDevice/UsbDescriptors.h` and add CDC-ACM descriptors
   alongside the proven control-device descriptors.
3. Add `drivers/NativeUsbDevice/AliothUsbBoard.h` containing alioth board data
   separately from controller mechanics: `0xa600000`, stream `0x00`, HS PHY
   `0x88e3000`, maximum high speed, secondary controller disabled.
4. Add `drivers/NativeUsbDevice/NativeUsbDevice.cpp`, initially in warm
   device mode: preserve the bootloader's already configured clocks/PHY,
   initialize event/transfer rings and endpoint 0, then enumerate. Do not
   reset the wrapper, program the disabled QMP PHY, or copy Uke stream `0x40`.
5. Add a host-side test for descriptor lengths, endpoint directions, ring
   wrap, TRB ownership, and short-packet termination before enabling writes.

Pass evidence:

- A host enumerates the expected alioth VID/PID and CDC-ACM interface.
- Host can request a small identity/version line and receive it without loss.
- A bounded console stream remains live for at least ten minutes and resumes
  after a cold boot.
- Before/after SMMU state shows only the expected stream `0x00` mapping
  behavior.

Rollback:

- Disable the USB service by its own boot argument and retain the frozen
  framebuffer/panic console.

Later stages:

- Add CDC-ECM/UDP logging only after CDC-ACM is reliable.
- Defer xHCI/host mode until device-mode logging is stable and a host-mode use
  case exists. High-speed USB is the current hardware limit.

### M2: PSCI SMP, one secondary first

Primary donor:

- ios-sweet `xnu/patches/0018-sweet-boot-cpu-driver.patch` from commit
  `ebbcc507ae22f9787af18a29b06ee061d798acdc` context.
- ios-sweet `xnu/patches/0065-sweet-smp-psci.patch` at
  `262e84420b34bbb0e516bf78f8d003448b22817a`.

Local integration points:

- `drivers/ArmCpu/ArmCpu.cpp`
- `drivers/ArmCpu/` topology/board data added by the implementation

Recommended first code changes:

1. Add an alioth CPU topology table for MPIDR `0x000` through `0x700`; do not
   reuse Sheng/Sweet core counts.
2. Add an alioth-specific opt-in boot argument and start only CPU 1.
3. Keep the working boot CPU path untouched and preserve the current
   `cpus=1` baseline as the rollback.
4. Verify PSCI `CPU_ON` calling convention, entry address, cache ownership,
   GIC redistributor mapping, timer frequency, and IPI delivery from alioth
   evidence before widening.

Pass evidence:

- CPU 1 executes the expected kernel entry and `initCPU`.
- CPU 1 receives its timer interrupt and a directed SGI/IPI.
- CPU 1 survives sustained scheduling and a cold boot.
- Only after this passes, extend incrementally to all eight MPIDRs.

Rollback:

- Boot with `cpus=1`; no persistent state is needed.

Caveat:

- `0x00000000` from PSCI is not proof. Accept only in-kernel execution and
  interrupt evidence.

### M3: PM8150 power and volume buttons through SPMI/PDC

Architecture donors:

- Sheng `package/ButtonProbe.cpp` at
  `df7b774b629c4ddf57981ff7a695645ed6ae55d3`.
- Sheng `package/HidInputProbe.cpp` at the same ref for the HID service path.
- Sheng config wiring at
  `6d22fa7b6968c21052f610f1c1795ac8f8f7a2dd`.

Local integration points:

- New alioth button/SPMI provider, preferably under
  `drivers/NativeButtons/`.
- `drivers/NativeHidInput/NativeHidInput.cpp` for the final report sink.

Recommended first code changes:

1. Add a non-mutating SPMI arbiter/PDC discovery probe that reports version,
   EE count, owned APIDs, PON base, and alioth PDC mapping.
2. Resolve PM8150 PON and GPIO APIDs dynamically on SID 0. Do not copy Sheng
   APIDs, PDC pin 1, or SPI 481.
3. Start with volume-up GPIO6 active-low, then PON `KPDPWR`; treat `RESIN` as
   volume-down only after alioth DT/readback verification.
4. Publish level transitions, not polling guesses, into the existing HID
   report contract.
5. Mask and log an interrupt storm rather than spinning in the handler.

Pass evidence:

- Power, volume-up, and volume-down each report correct press and release
  independently.
- A held button does not repeat as an endless stream.
- No unrelated APID is read after an external abort or arbitration error.
- Button input still works after a cold boot and does not depend on touch.

Rollback:

- Keep the alioth buttons provider behind a default-off boot argument until
  all three inputs pass.

### M4: GOP/IOSurface/IOMobileFramebuffer CPU presentation

Donor commits:

- `ebbcc507ae22f9787af18a29b06ee061d798acdc`: IOSurface and
  IOMobileFramebuffer CPU display path.
- `2e77dccf1ac3acc29f42c82731b61fdc8e6c2a2a`: throttled present semantics.
- `5ea54c33dd6f551f806ffe095355d9a5f2a3e61e`: iPhone X logical panel
  identity at 1125x2436.
- `347e19018e3e4c0117635e9e444f73659b2ece53`: display logical power request.

Donor files:

- ios-sweet `xnu/patches/0046-sweet-iosurface-iomfb.patch`
- ios-sweet `xnu/patches/0056-sweet-fb-throttled-present.patch`
- ios-sweet `xnu/patches/0061-sweet-iphone-x-display-size.patch`
- ios-sweet `xnu/patches/0079-sweet-display-power.patch`

Local integration points:

- `drivers/NativeMobileDisplay/NativeGOPPresenter.cpp`
- `drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp`
- `drivers/NativeMobileDisplay/NativeGOPWire.h`
- `drivers/NativeMobileDisplay/OriginalMobileFramebufferABI.h`

Recommended first code changes:

1. Treat `NativeLegacyDisplay` and `NativeGOPPresenter` as the backend
   foundation; keep the incomplete `NativeMobileDisplay.cpp` prototype off.
2. Port the IOSurface/IOMobileFramebuffer service contract separately from
   the scanout backend.
3. Connect the service to the alioth GOP framebuffer at `0x9c000000`, size
   `0x2300000`.
4. Present iPhone X logical 1125x2436 into the physical 1080x2400 panel with
   explicit coordinate/stride transforms.
5. Implement swap begin/end/retire, vsync, default-surface publication, and
   logical power selector before attempting panel takeover.
6. Do not program K11A DSI/DSC, backlight, PHY, or panel sequencing in this
   milestone.

Pass evidence:

- Exactly one expected IOMobileFramebuffer service appears.
- A default surface exists and a visible frame reaches the screen.
- Multiple swaps retire without stale-frame or use-after-free behavior.
- Vsync delivery is observed and SpringBoard/backboardd does not
  reject the service.
- Logical power off/on does not corrupt the next frame.

Rollback:

- Disable the display service and retain the UniXNU text/panic console.

### M5: FocalTech FTS over GENI SPI, then HID

Protocol references:

- ios-sweet `xnu/patches/0060-sweet-keys-touch-drivers.patch`, especially the
  `touch_focaltech()` parser, only for FTS report semantics.
- ios-sweet `xnu/patches/0058-sweet-hid-event-services.patch` for the HID
  event-service contract.
- ios-sweet `xnu/patches/0071-sweet-touch-edge.patch` for touch lifetime and
  edge-tip semantics.
- Sheng `package/HidInputProbe.cpp` at
  `df7b774b629c4ddf57981ff7a695645ed6ae55d3` for service publication
  diagnostics.

Local integration points:

- `drivers/NativeGeniSpi/GeniSpi.c`
- `drivers/NativeGeniSpi/GeniSpi.h`
- `drivers/NativeHidInput/NativeHidInput.cpp`
- New alioth FTS client under `drivers/NativeFocalTech/`

Recommended first code changes:

1. Add an alioth GENI SPI instance description for `spi@990000`, CS0, and
   the 6 MHz clock relationship.
2. Add a transport-only FTS probe: reset GPIO33, IRQ GPIO39, chip-ID/readback,
   power state, and report-ready interrupt. Do not publish HID yet.
3. Add the FTS report parser for 10 contacts and raw axes
   `0x21c0 x 0x4b00`.
4. Parameterize `NativeHidInput` axes and source identity. Current
   `30480x20320` and Sheng globals are not alioth values.
5. Publish one Contact/Touch Screen digitizer service, then add edge-tip for
   contacts that begin near an edge.

Pass evidence:

- One-contact corner tests map correctly to logical 1125x2436 coordinates.
- Ten-contact tracking reports stable IDs and releases.
- A contact that begins at an edge keeps edge-tip for its full lifetime.
- Touch works after a cold boot and button input remains independent.

Rollback:

- Keep touch behind a default-off boot argument and preserve the current HID
  service without an alioth FTS client.

Caveat:

- The Goodix node on alioth belongs to the fingerprint sensor. Do not port
  Sweet's Goodix GT9896 transport as the touch solution.

### M6: NativeUfs/SMMU root-storage decision and regression

Donor history:

- ios-sweet `33aceac8fef50c0b1688e20a7f1a5bfea023c10e`: read-only UFS root
  device.
- ios-sweet `b833a55299f6c00c4a100c231a6ff8904048ab6a`: writes behind
  `ufsrw=1`.
- ios-sweet `2bde9cd2ec111825f6fc1c07216df9b3b48c4971`: `rootrw=1`.
- ios-sweet `65bb6ea78df3af88cebbd9103c16837ceb5f872a`: high-DDR kernel
  placement, not a storage primitive.

Local integration points:

- `drivers/NativeUfs/NativeUfs.cpp`
- `drivers/NativeUfs/SmmuTakeover.h`
- `drivers/NativeUfs/RootLayout.h`
- `drivers/NativeUfs/GccUfs.h`

Architecture decision:

- Current NativeUfs publishes verified read-only `SHNGROOT`/`OS`/`SystemOS`
  windows.
- The current alioth board selects a whole-`userdata` journaled HFS+ volume
  with `rd=ufsroot0 ufsrw=1 rootrw=1`.
- Freeze the passing ios-sweet proof first. Then explicitly choose one:
  `SHNGROOT` windows or whole-`userdata` HFS+. Do not publish both roots or
  mix their sector mappings.

If whole-`userdata` HFS+ remains the selected route:

1. Add a whole-partition `IOBlockStorageDevice` for GPT `userdata` with a
   512-byte logical view over 4096-byte media.
2. Bound every read and write by the GPT first/last LBA.
3. Preserve the two alioth SMMU streams `0x00e0` and `0x04e0`; reuse their
   exact exclusive bypass contexts when still valid. Never create `0x60` or
   `0x10`.
4. Add and test WRITE(10), forced-unit completion, synchronize-cache, short
   transfer, and error recovery before any persistent image is accepted.
5. Keep the existing probe diagnostics and record controller/SMMU state
   before and after every ownership change.

Pass evidence:

- GPT partition is found by name/GUID and its geometry matches the audited
  first/last LBA and byte capacity.
- A 512-byte sector read path is correct at partition start, interior, and
  final sector, with no read outside the partition.
- Bounded writes followed by readback and SYNCHRONIZE CACHE succeed without
  stepping outside `userdata`.
- The intended HFS+ root mounts read-write, files are written and re-read,
  and a cold boot preserves or correctly replays the journal.
- A rollback/recovery image is hash-identified before the first persistent
  write.

Rollback:

- Keep the frozen ios-sweet proof and recovery image. Revert to its exact
  artifacts if a cold boot fails.

## 5. Acceptance matrix

| Milestone | Minimum hardware proof | Explicit non-proof |
| --- | --- | --- |
| M0 | Frozen artifacts cold-boot to the same stage | A previous successful boot without matching hashes |
| M1 | Host enumerates CDC-ACM and receives live console lines | Ring code passes CI |
| M2 | Secondary CPU executes `initCPU`, takes timer and IPI, survives cold boot | PSCI returns success |
| M3 | Power/up/down independently produce correct press/release HID state | PMIC registers read |
| M4 | Default surface presents, swaps retire, vsync and power transition work | `AppleCLCD` service registers |
| M5 | Correct corner mapping, 10 contacts, edge-tip, cold boot | SPI transfers complete |
| M6 | Bounded read/write/cache test plus HFS+ cold-boot journal result | GPT reads or `fastboot OKAY` |

## 6. Recommended first implementation batch

Start with M1, and split it into two reviewable changes:

1. **Mechanics-only port**
   - Add `drivers/NativeUsbDevice/UsbRing.h`.
   - Add `drivers/NativeUsbDevice/UsbDescriptors.h` with control and
     CDC-ACM descriptors.
   - Add host tests for descriptors, ring ownership, wrap, and short packets.
   - No MMIO access and no device image change.
2. **Alioth warm device-mode integration**
   - Add `drivers/NativeUsbDevice/AliothUsbBoard.h`.
   - Add `drivers/NativeUsbDevice/NativeUsbDevice.cpp`.
   - Use `0xa600000`, SMMU stream `0x00`, HS PHY `0x88e3000`, and high-speed
     only.
   - Enumerate first; add CDC-ACM transmit only after EP0 is stable.
   - Capture before/after state and keep the service default-off.

Do not combine the CDC-ECM network interface, live UDP logger, xHCI host
mode, or any UFS write change in this first batch.

## 7. Non-goals

- Linux userland, ext4/tar, systemd, Ubuntu, simpledrm, or Linux partition
  schemes.
- Sweet/Sheng MMIO sequences, GPIO numbers, APIDs, PDC pins, SPIs, panel
  memory, clocks, rails, or SMMU streams copied without alioth evidence.
- GPU acceleration, DSI/DSC panel takeover, or backlight work in the first
  display milestone.
- USB host/xHCI, secondary USB controller, or SS PHY work in M1.
- UFS write optimization, persistent logging in the root tail, or GPT
  repartitioning before M6's bounded I/O and cold-boot gates pass.
- Reopening passed UEFI, UniXNU, DarwinBoot, or XNU entry stages because of
  an unrelated later failure.

## 8. Primary references

- UniXNU upstream:
  `https://github.com/UniXNU/UniXNU/tree/8a6672f7fa4ad6c41f9c5cca32992bc618752638`
- UniXNU core fork:
  `https://github.com/FFT-infra/UniXNU-core-alioth/tree/bd78ae5a69e9f00459016f9cf27ed8798ff24d93`
- NativeUfs upstream:
  `https://github.com/UniXNU/NativeUfs/tree/af785c1777161a62931425c2faaa536ce0bebd5b`
- ios-sweet:
  `https://github.com/dipa-studio/ios-sweet/tree/067c9b3d463ecfbe37520683637ea2e6af31f3a4`
- Uke USB branch:
  `https://github.com/Allenwdk/UniXNU-uke/tree/ce8498a06d6737d0558521a8635b8f5dfb417e11`
- Sheng:
  `https://github.com/UniXNU/UniXNU-sheng/tree/9c88b90155ea5e400f5f75d06104dee96260619a`
- Guacamole:
  `https://github.com/wochgn/UniXNU-guacamole/tree/6269d315dbf43c4f7334177721f5c18e9b633f97`
- Pipa:
  `https://github.com/xVanTuring/UniXNU-pipa/tree/6749f2fb443f81e64f76bf28406265b8e8c5d0e2`
