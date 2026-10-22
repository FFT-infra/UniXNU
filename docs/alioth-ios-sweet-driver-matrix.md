# alioth ios-sweet driver-intake matrix

Date: 2026-10-23

Scope: read-only source audit for moving the Darwin 22 / iOS 16.7.16
`20H392` route from ios-sweet's SM7150 "sweet" board to Xiaomi
`alioth` / SM8250. No code was changed, no test was run and the device was
not touched.

## Verdict

The ios-sweet proof is valuable at the Darwin/BSD contract level, not as a
drop-in SM8250 driver package.

- Reuse its BSD root-device pattern, HFS+ integration, writable-root
  semantics, IOSurface/IOMobileFramebuffer ABI, IOHID service/edge-tip
  semantics and PSCI CPU_ON structure.
- Replace every Sweet controller/PHY/panel/touch/button implementation with
  an alioth implementation. Sweet's MMIO, clocks, rails, pins, panel memory
  and USB wrapper are not alioth facts.
- The immediate root blocker is architectural. alioth NativeUfs currently
  publishes verified, read-only SHNGROOT/OS/SystemOS windows. The selected
  `rd=ufsroot0 ufsrw=1 rootrw=1` route instead needs the whole GPT
  `userdata` partition exposed as a writable BSD disk carrying a journaled
  HFS+ volume.
- Display is the next GUI blocker. alioth has a framebuffer/GOP foundation,
  but no published Darwin 22 IOMobileFramebuffer/AppleCLCD service.
- Touch and buttons are alioth-only. Sweet's Goodix GT9896 over GENI-I2C and
  PM6150/PM6150L SPMI providers do not match alioth's FocalTech over
  GENI-SPI and PM8150-family button wiring.
- SMP is not a prerequisite for the first root mount. Enable a second
  alioth CPU only after the Darwin 22 CPU path is audited; do not scale
  straight to eight cores.
- USB is a useful later debug transport, not a root bring-up prerequisite.
  Sweet's DWC3/QUSB2/QSCRATCH programming is not transferable without
  alioth evidence.

## Source snapshot and classification

Inspected source snapshots:

| Source | Snapshot used by this audit | Role |
| --- | --- | --- |
| `dipa-studio/ios-sweet` | `067c9b3d463ecfbe37520683637ea2e6af31f3a4` | Hardware-proven sweet driver and ABI reference |
| `UniXNU/UniXNU` public tree | `8a6672f7fa4ad6c41f9c5cca32992bc618752638` | Upstream driver boundaries and submodule pins |
| Local UniXNU core | `fb042b900412a87b7caa28dea8e6857c861deb3a` | Checked-out shared tree; existing dirty files preserved |
| Local alioth device repo | `c2cefd11654e2a19355f31ba1b86436fcf0ac262` | alioth board/config state |
| Local NativeUfs fork | `6871d6467e6d98dfef407de2485438d121b0000b` on `feat/alioth-ufs-probe` | alioth stream/GPT probe implementation |
| `UniXNU/UniXNU-sheng` | `9c88b90155ea5e400f5f75d06104dee96260619a` | SM8550/sheng reference, not alioth |
| `Allenwdk/UniXNU-uke` | `0fde2b1b53b871e9b33b1b242a7034f56e2c71c4` | UFS/GPT and Darwin 23 root evidence |
| `wochgn/UniXNU-guacamole` | `6269d315dbf43c4f7334177721f5c18e9b633f97` | ios-sweet integration on another Qualcomm phone |

Classification labels used below:

- `Darwin contract`: generic Darwin/BSD/IOKit semantics; no Sweet MMIO.
- `Sweet hardware`: proven on sweet, but tied to its SoC, wrapper, clocks,
  rails, panel or pins.
- `alioth only`: not yet implemented or qualified for alioth.
- `unknown`: source exists, but no evidence is sufficient for alioth use.

Evidence paths are repository-relative. Base directories are:

- `ios-sweet/`
- `UniXNU/`
- `UniXNU-alioth/`
- `uefi-alioth/`

## 1. UFS / HFS root intake

| Patch/driver | Function | Portable vs Sweet-specific hardware | alioth replacement needed | Evidence/source path | Risk |
| --- | --- | --- | --- | --- | --- |
| `0026-stage-hfs.sh` + `0026-sweet-builtin-hfs.patch` | Stages Apple OSS `hfs-627.100.6` at commit `31d5ec238dfab485e78a9a30b8b92d13e8914f03`, links it into XNU, registers it before root mount, and provides built-in IOKit glue | Darwin contract plus a small Sweet build wrapper. The HFS core is not board hardware | No controller replacement. Rebase the registration/config glue to the current Darwin 22 kernel and preserve journal replay | `ios-sweet/xnu/patches/0026-stage-hfs.sh:15`, `ios-sweet/xnu/patches/0026-sweet-builtin-hfs.patch:103` | Medium. HFS source and XNU release must remain version-compatible; do not casually substitute hfsprogs behavior for XNU's HFS implementation |
| `0028-sweet-ufs-root-device.patch` | Takes over the bootloader's UFSHCI state, finds GPT `userdata`, exposes a BSD `ufsroot0`/`rufsroot0` with 512-byte sectors over 4 KiB media, and selects it with `rd=ufsroot0` | Contract is portable; controller bring-up, `/sweet-ufs` node, clocks and DMA assumptions are Sweet-specific | Yes. Keep the BSD frontend contract but publish alioth GPT `userdata` from NativeUfs; do not import the Sweet host takeover | `ios-sweet/xnu/patches/0028-sweet-ufs-root-device.patch:23`, `:53`, `:557` | High. Current alioth NativeUfs publishes only manifest-selected windows and is read-only; merely adding the boot argument cannot mount HFS |
| `0030-sweet-ufs-writes.patch` | Adds opt-in `ufsrw=1`, WRITE(10), range checks, write strategy and SYNCHRONIZE CACHE | Block write policy is portable; command queue and cache operations are Sweet-controller code | Yes. Implement equivalent write and synchronize operations in alioth NativeUfs after the whole-partition device exists | `ios-sweet/xnu/patches/0030-sweet-ufs-writes.patch:17`, `:80`, `:97` | Critical. Wrong LBA projection or cache handling can corrupt Android `userdata`; writes must remain strictly inside the selected partition |
| `0031-sweet-read-write-root.patch` | Clears `MNT_RDONLY` for `rootrw=1`; permits a dirty non-journaled root to mount read-write because no pre-root fsck exists | Portable Darwin root policy, with an explicitly unsafe exception | Reuse the `rootrw=1` root-mount behavior. Prefer retaining the journal and do not generalize the dirty-volume exception beyond the qualified root | `ios-sweet/xnu/patches/0031-sweet-read-write-root.patch:14`, `:33` | High. A non-journaled dirty volume can amplify any lost-write bug. The primary image is journaled HFS+; verify journal state before boot |
| `0034-sweet-ufs-dma-above-4g.patch` | Checks UFSHCI `CAP_64AS` before allowing DMA above 4 GiB and reports the DMA address | The capability check is portable; actual DMA address/TCR/SMMU constraints are alioth-specific | Adapt to NativeUfs, its two SM8250 SMMU streams and current DMA allocation | `ios-sweet/xnu/patches/0034-sweet-ufs-dma-above-4g.patch:24` | High. alioth probe 7 found `sid00e0` and `sid04e0`; probe 10 showed safe bypass mappings, but write DMA and cache ownership still need qualification |
| `0041-sweet-hfs-link-notes.patch` | Reduces HFS hard-link repair logging | Portable diagnostic behavior | Reuse or fold into the HFS port | `ios-sweet/xnu/patches/0041-sweet-hfs-link-notes.patch:9` | Low |
| `0052-sweet-hfs-rename-swap.patch` | Maps `RENAME_SWAP` to `VNOP_EXCHANGE` so Foundation atomic writes work on HFS | Portable VFS/HFS compatibility contract | Reuse, subject to Darwin 22 VFS API review | `ios-sweet/xnu/patches/0052-sweet-hfs-rename-swap.patch:9`, `:18` | Medium. Omitting it causes Foundation atomic-write failures; incorrect semantics can damage file identity |
| `0069-sweet-ufs-faster.patch` | Sweet-specific polling, cache-clean and log-flush tuning | Sweet hardware | Do not import as a driver-intake prerequisite. Measure alioth first | `ios-sweet/xnu/patches/0069-sweet-ufs-faster.patch:43`, `:55` | Medium. Premature tuning can hide transport ordering bugs |
| `0047-sweet-persistent-console-log.patch` | Writes a 4 MiB console ring to the last blocks of `userdata` | Sweet design; hardware-independent but storage-layout invasive | No. Use framebuffer/RAM console first; add a later non-destructive debug path | `ios-sweet/xnu/patches/0047-sweet-persistent-console-log.patch:27`, `:34` | High. It imposes a hidden tail reservation and conflicts with a whole-partition HFS layout or later root-image growth |
| alioth `NativeUfs` today | Implements UFS transport, GPT probing, SMMU verification, raw reads and verified SHNGROOT/OS/SystemOS windows | alioth hardware path, not ios-sweet | Yes. Add a whole-`userdata` read/write BSD disk and root-device publication; preserve existing probe diagnostics | `UniXNU/drivers/NativeUfs/NativeUfs.cpp:1396`, `:1437`, `:1659`, `:2165`; `UniXNU/drivers/NativeUfs/RootLayout.h:193` | Critical. The current `NativeUfsBlock` rejects writes and `startRoot` requires a valid SHNGROOT manifest. The selected HFS route has neither condition |

### UFS/HFS intake decision

The correct intake unit is:

1. Keep the alioth NativeUfs transport, SMMU reuse and GPT parser.
2. Add a whole-GPT-partition BSD device with a 512-byte logical sector view.
3. Add bounded WRITE(10), forced-unit/cache-sync and error recovery.
4. Register `ufsroot0` for `rd=ufsroot0`.
5. Build a case-sensitive, journaled HFS+ root and mount it read-write with
   `rootrw=1`.
6. Carry the HFS fixes required by Foundation, especially atomic rename and
   hard-link metadata handling.

Do not replace this with Sweet's controller code. The shared parts are the
BSD interface, root selection, write safety and filesystem semantics.

### alioth root facts already established

- UFS controller base is `0x1d84000`, compatible with `qcom,ufshc`.
- SM8250 UFS SMMU streams observed on hardware are `0x00e0` and `0x04e0`.
- Probe 7 found exact, exclusive bypass context mappings and probe 10 reused
  them without changing their before/after values.
- GPT `userdata` is `first_lba=0x2a8000`,
  `last_lba=0x3b037fa`, `lba_bytes=4096`.
- Evidence: `UniXNU-alioth/PLAN.md:133`, `:140`,
  `UniXNU/drivers/NativeUfs/README.md:5`.

## 2. Framebuffer / IOSurface / IOMFB intake

| Patch/driver | Function | Portable vs Sweet-specific hardware | alioth replacement needed | Evidence/source path | Risk |
| --- | --- | --- | --- | --- | --- |
| `0046` IOSurface ABI half | Publishes `IOCoreSurfaceRoot`, implements the iOS 16.7 `IOSurfaceRootUserClient` selectors, buffer ownership, ports and per-client mappings | Darwin/iOS ABI contract; no board MMIO | Yes at the integration layer: implement or qualify the same service contract in the alioth kernel, with no Sweet global names required | `ios-sweet/xnu/patches/0046-sweet-iosurface-iomfb.patch:53`, `:1006` | High. This is largely reconstructed closed ABI. Wrong retain, port or lock-result semantics break CoreAnimation |
| `0046` `SweetFramebuffer.cpp` half | Publishes an `AppleCLCD`/IOMobileFramebuffer service over the bootloader framebuffer, copies enabled layer surfaces into panel memory and emits a timer-based vsync | IOSurface/IOMFB surface semantics are portable; fixed panel memory and direct CPU-copy backend are Sweet-specific | Yes. Keep the service ABI, replace the backend with alioth GOP/K11A integration | `ios-sweet/xnu/patches/0046-sweet-iosurface-iomfb.patch:996`, `:1121`; `ios-sweet/README.md:20` | High. A service that registers but cannot present, retire swaps or provide the default surface will strand backboardd/SpringBoard |
| `0056-sweet-fb-throttled-present.patch` | Records the latest swap and presents at a controlled rate rather than copying every 60 Hz swap | Portable scheduling/presentation design | Reuse the throttling concept, not the Sweet boot argument or fixed copy cost | `ios-sweet/xnu/patches/0056-sweet-fb-throttled-present.patch:53`, `:60` | Medium. Too-frequent copies consume the CPU; a stale pending record can also lose swap semantics if retirement is wrong |
| `0068-sweet-surface-lookup-cached.patch` | Makes repeated lookup of an already-mapped IOSurface return the existing client state instead of remapping/counting incorrectly | Portable IOSurface user-client behavior | Reuse as an ABI conformance test case | `ios-sweet/xnu/patches/0068-sweet-surface-lookup-cached.patch:17` | Medium. Incorrect behavior gives IOSurface.framework base address `-1` and later client failures |
| `0079-sweet-display-power.patch` | Handles IOMFB selector 12 and logically powers the display off/on; Sweet fades the physical frame buffer | Selector semantics portable; panel-power behavior Sweet-specific | Implement the selector, but use alioth K11A/backlight or command-mode policy rather than frame fading | `ios-sweet/xnu/patches/0079-sweet-display-power.patch:9`, `:38` | Medium. Treating logical power change as a WinCE-style pixel fade is not alioth panel control |
| `0061-sweet-iphone-x-display-size.patch` | Presents a 1125x2436 iPhone X logical panel over the 1080x2400 Sweet panel and maps touch coordinates into it | iPhone X userland compatibility is portable; fixed Sweet panel geometry/stride is not | Keep logical identity only after verifying alioth is 1080x2400; do not copy panel memory assumptions | `ios-sweet/xnu/patches/0061-sweet-iphone-x-display-size.patch:8`, `:17`, `:43` | Medium. A logical display that does not correspond to the actual scanout produces clipped or offset UI |
| alioth `NativeLegacyDisplay` | Uses the original legacy framebuffer family, validates swaps, presents GOP frames, can emit vsync and optionally hands frames to a GPU client | alioth foundation, but it openly says it is not yet a published system display | Yes. Qualify and publish the correct iOS 16 IOMFB service, or finish the class and its provider contract | `UniXNU/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:3`, `:40`; `UniXNU/drivers/NativeMobileDisplay/README.md:3` | High. The local code is evidence of a candidate backend, not an alioth display success |
| alioth `NativeMobileDisplay` prototype | Validates framebuffer metadata and implements only a small part of the original IOMobileFramebuffer ABI | alioth prototype | Yes. It is not a complete service and numerous selectors panic | `UniXNU/drivers/NativeMobileDisplay/NativeMobileDisplay.cpp:6`, `:27` | High. Do not enable it as a userland display driver |

### alioth display facts

- The selected panel is K11A command-mode DSI with DSC, 1080x2400.
- Evidence: `uefi-alioth/Resources/DTBs/alioth.dts:30231`, `:30295`,
  `:39468`.
- alioth currently disables mobile display in the kernel configuration and
  says there is no display driver yet:
  `UniXNU-alioth/configs/alioth_defconfig:29`,
  `UniXNU-alioth/board.toml:125`.
- The first qualified target should be the firmware framebuffer path used by
  the legacy display family. K11A DSI/DSC panel takeover is a later variable,
  not part of the first IOMFB publication gate.

## 3. HID / touch / buttons intake

| Patch/driver | Function | Portable vs Sweet-specific hardware | alioth replacement needed | Evidence/source path | Risk |
| --- | --- | --- | --- | --- | --- |
| `0058-sweet-hid-event-services.patch` | Supplies the iOS 16.7 IOHIDFamily event-service/user-client ABI and Sweet key/touch service classes | Event ABI and queue semantics are portable; the final key/touch providers are Sweet-specific | Yes at provider level. Preserve the service contract; replace Sweet hardware glue | `ios-sweet/xnu/patches/0058-sweet-hid-event-services.patch:7`, `:18`; `ios-sweet/README.md:22` | High. An IOHID ABI mismatch can leave SpringBoard without touch or lock events |
| `0071-sweet-touch-edge.patch` | Marks contacts that begin near a panel edge with `kIOHIDDigitizerEventFromEdgeTip` for their whole life | Portable digitizer/UIKit semantics | Reuse with alioth distance thresholds derived from its touch scale | `ios-sweet/xnu/patches/0071-sweet-touch-edge.patch:8`, `:42` | Medium. Without it the gesture/home UI does not work; applying Sweet pixel thresholds blindly gives wrong edge bands |
| `0060` + `0063` Goodix path | Polls sweet's Goodix GT9896 over GENI-I2C, configures clocks/GPIO, acknowledges reports and parses contacts | Sweet hardware | No. alioth uses a different touch transport and protocol | `ios-sweet/xnu/patches/0060-sweet-keys-touch-drivers.patch:219`; `ios-sweet/xnu/patches/0063-sweet-goodix-geni-packing.patch:16` | High. Register offsets, FIFO packing, reset/IRQ GPIO and report protocol are not interchangeable |
| `0060`/`0062` buttons | Polls Sweet PMIC PON/GPIO and emits HID consumer keys | Event mapping is portable; PMIC and GPIO layout are Sweet-specific | Yes. Implement alioth PM8150 PON and PM8150 GPIO6 providers | `ios-sweet/xnu/patches/0060-sweet-keys-touch-drivers.patch:137`; `ios-sweet/xnu/patches/0062-sweet-touch-power-battery.patch:479` | Medium. Wrong SPMI APID or GPIO polarity can produce stuck keys or miss wake/lock |
| alioth `NativeGeniSpi` | Generic GENI SPI FIFO driver with no board addresses, clocks or pins | Darwin/controller code, board-neutral | Yes. Wire it to alioth SE `spi@990000`, clocks, pins, IRQ and a FocalTech protocol client | `UniXNU/drivers/NativeGeniSpi/GeniSpi.h:1`; `UniXNU/drivers/NativeGeniSpi/README.md:3` | Medium. The transport exists, but "driver builds" is not a touch success |
| alioth `NativeHidInput` | Publishes HID reports from a shared touch snapshot | Service layer is reusable; current coordinate maxima and sink are sheng assumptions | Yes. Bridge FocalTech frame updates and change axes to alioth touch resolution | `UniXNU/drivers/NativeHidInput/NativeHidInput.cpp:136`, `:159` | High. It currently normalizes to `30480x20320` and depends on the sheng `TouchProbe.cpp`; it is not in alioth's archive list |

### alioth touch and button facts

- Touch is `focaltech,fts` on `spi@990000`, not Goodix over I2C.
- Reset GPIO is `0x21`, IRQ GPIO is `0x27`, maximum contacts are 10, and
  reported coordinates are `0x21c0 x 0x4b00` (`8640 x 19200`).
- Evidence: `uefi-alioth/Resources/DTBs/alioth.dts:33218`, `:33237`,
  `:33250`, `:33253`.
- Power is PM8150 `KPDPWR`; the second PON input is `RESIN`, which is the
  likely volume-down source. Volume-up is PM8150 GPIO6.
- Evidence: `uefi-alioth/Resources/DTBs/alioth.dts:4193`, `:4197`,
  `:4319`.
- The alioth ADT already requests `hold`, `voldown`, `volup` and declares no
  Home button: `UniXNU-alioth/scripts/make-alioth-adt.py:182`.

The first touch milestone should be one contact at the four corners with
correct logical 1125x2436 coordinates, followed by ten-contact tracking and
edge-tip state. Buttons should be qualified independently from touch.

## 4. SMP / PSCI intake

| Patch/driver | Function | Portable vs Sweet-specific hardware | alioth replacement needed | Evidence/source path | Risk |
| --- | --- | --- | --- | --- | --- |
| `0017-sweet-device-tree-virtual-timer-irq.patch` | Reads the virtual timer INTID from the device tree instead of baking one vector into the kernel | Portable Darwin/GIC design | Yes, as an alioth DT/ADT and interrupt-map qualification step | `ios-sweet/xnu/patches/0017-sweet-device-tree-virtual-timer-irq.patch:30`, `:52` | High. Sweet uses INTID 19. alioth's Linux DT lists timer PPIs 13/14/11/12. Do not infer the XNU-used vector from a filename or from sweet |
| `0018-sweet-boot-cpu-driver.patch` | Adds the initial IOCPU/IOCPUInterruptController boot-CPU driver and GIC/timer skeleton | Mostly portable Darwin IOCPU plumbing | Reuse only after the Darwin 22 private ABI and alioth interrupt path are checked | `ios-sweet/xnu/patches/0018-sweet-boot-cpu-driver.patch:1`, `:139` | High. The local `ArmCpu` cites Darwin 23 private behavior; boot-CPU work can regress a currently working one-CPU path |
| `0065-sweet-smp-psci.patch` | Starts secondary cores with PSCI CPU_ON, per-target MPIDR affinity and the kernel reset vector; waits for initCPU/GIC/SGI evidence | PSCI contract is portable; topology and Sweet core mask are Sweet-specific | Yes. Adapt alioth MPIDR topology and start one secondary first | `ios-sweet/xnu/patches/0065-sweet-smp-psci.patch:28`, `:205`, `:230`; `ios-sweet/README.md:16` | High. Wrong affinity, cache maintenance or reset-vector mapping can hard-hang the board |
| `0078-sweet-smp-8-cores.patch` | Raises Sweet's six-A55 limit to all eight cores, including two A76 cores | Sweet hardware/policy | No. alioth is a different 4+3+1 topology and needs its own qualification | `ios-sweet/xnu/patches/0078-sweet-smp-8-cores.patch:11`, `:26` | High. Copying this masks topology errors and tests too many variables at once |
| alioth `ArmCpu` today | Supports GICv3 and PSCI, but secondary nubs are opt-in through `sheng-smp` and remote GIC setup through `sheng-smp-gic` | alioth-capable code with sheng-specific controls | Yes. Add a board-neutral or alioth shim after the ABI audit; start with CPUs 0 and 1 | `UniXNU/drivers/ArmCpu/ArmCpu.cpp:37`, `:188`, `:409`, `:760` | High. The current alioth ADT exports one CPU and `cpus=1`; this is deliberate until the Darwin 22 path is qualified |

### alioth CPU facts

- Linux DT declares cores `0x000..0x700` with `enable-method = "psci"`,
  PSCI 1.0 over SMC, GICv3, and eight MPIDR-affinity values.
- Evidence: `uefi-alioth/Resources/DTBs/alioth.dts:38`, `:42`,
  `:230`, `:2769`, `:39458`.
- alioth currently requests only the boot CPU:
  `UniXNU-alioth/board.toml:169`, `:149`.

SMP acceptance is a new CPU entering `initCPU`, taking its timer and SGI/IPI,
then surviving a cold boot. PSCI's return code alone is insufficient.

## 5. USB / logging intake

| Patch/driver | Function | Portable vs Sweet-specific hardware | alioth replacement needed | Evidence/source path | Risk |
| --- | --- | --- | --- | --- | --- |
| `0072-sweet-usb-probe.patch` | Read-only DWC3/QUSB2/GDSC/clock probe | Sweet hardware and probe methodology | No direct reuse. Reuse only the "observe before programming" discipline | `ios-sweet/xnu/patches/0072-sweet-usb-probe.patch:33`, `:54`, `:72` | Medium. Reading another board's PHY and power registers is diagnostic noise, not proof |
| `0073-sweet-usb-device-ep0.patch` | Takes DWC3 device mode, builds an event ring, configures physical EP0 and implements setup/data/status stages | DWC3 state machine is mostly portable; wrapper/clock/PHY programming is board-specific | Yes. Reimplement on the alioth DWC3 core and wrapper after power/clock evidence | `ios-sweet/xnu/patches/0073-sweet-usb-device-ep0.patch:49`, `:313`, `:382` | High. Resetting or reconfiguring a wrapper with wrong clocks can hang enumeration |
| `0074-sweet-usb-vbus-override.patch` | Sets QSCRATCH HS/SS PHY VBUS override bits to make the controller connect | Sweet hardware | No. Obtain alioth wrapper/PHY behavior from alioth evidence | `ios-sweet/xnu/patches/0074-sweet-usb-vbus-override.patch:8`, `:27` | High. Direct QSCRATCH writes can override charger/role state incorrectly |
| `0075-sweet-usb-ecm.patch` | Adds CDC-ECM descriptors, endpoints and an in-kernel Ethernet interface | CDC-ECM protocol and network shaping are portable; transport setup is Sweet-specific | Yes, split protocol from alioth DWC3 transport | `ios-sweet/xnu/patches/0075-sweet-usb-ecm.patch:97`, `:111`, `:399`; `ios-sweet/NOTES.md:15` | Medium. Keep the proven packet/endpoint behavior, not the Sweet wrapper addresses |
| `0076-sweet-usb-rx-mbuf.patch` | Fixes RX allocation to use a valid mbuf cluster after the first empty mbuf caused a panic | Portable USB network fix | Reuse if the same receive design is used | `ios-sweet/xnu/patches/0076-sweet-usb-rx-mbuf.patch`; `ios-sweet/NOTES.md:15` | Low |
| `0077-sweet-usb-live-log.patch` | Streams the kernel console ring over USB ECM/UDP after the link is up | Logging design portable; depends on Sweet USB and console ring | Use later, after alioth USB works. Start with framebuffer/panic console | `ios-sweet/xnu/patches/0077-sweet-usb-live-log.patch:47`, `:101`; `ios-sweet/NOTES.md:13` | Medium. It is a debug convenience, not evidence that root or userland is healthy |
| `0081-sweet-usb-link-state.patch` | Follows link/connection state | Sweet hardware-specific behavior | Defer until alioth controller enumeration | `ios-sweet/xnu/patches/0081-sweet-usb-link-state.patch` | Medium |

### alioth USB facts

- alioth's DWC3 core base is also `0xa600000`, but that one common address is
  not sufficient to reuse the Sweet wrapper/PHY path.
- alioth uses USB30_PRIM GDSC at `0x10f004`, a high-speed PHY at
  `0x88e3000`, and a separate QMP/SS PHY at `0x88e8000`.
- Evidence: `uefi-alioth/Resources/DTBs/alioth.dts:18508`, `:18584`,
  `:18601`, `:3875`.
- The high-speed PHY is enabled; the SS PHY node is disabled and maximum
  speed is high-speed.

USB should be the final driver-intake item. It cannot compensate for an
unqualified root block device or an unpublished display.

## What is already demonstrated on ios-sweet

These are hardware-proven on sweet, not on alioth:

- Six Cortex-A55 cores through PSCI, 16 KiB pages, GICv3, timers, touch and
  buttons reach launchd and SpringBoard.
- A writable journaled HFS+ root in `userdata`, selected with
  `rd=ufsroot0 ufsrw=1 rootrw=1`.
- IOSurface plus IOMobileFramebuffer/AppleCLCD services, CPU rendering and
  presentation of a 1125x2436 iPhone X logical panel.
- Goodix touch and PMIC buttons as HID events.
- DWC3 device mode, CDC-ECM USB networking and live console streaming.

Evidence: `ios-sweet/README.md:11`, `:16`, `:18`, `:20`, `:22`,
`:23`; `ios-sweet/NOTES.md:13`, `:15`, `:17`.

This proves the Darwin/iOS contracts and the root-image design. It does not
prove any sweet MMIO sequence on alioth.

## What remains alioth-only work

- Convert alioth NativeUfs from verified read-only root windows to a
  whole-partition writable BSD root device.
- Add write caching, forced-unit completion and recovery to alioth UFS.
- Integrate a journaled HFS+ root and Darwin 22 root-mount behavior.
- Publish a complete Darwin 22 IOSurface/IOMobileFramebuffer service and a
  qualified alioth scanout backend.
- Wire FocalTech over alioth GENI-SPI and translate its reports to the
  IOHID/iPhone logical coordinate space.
- Add alioth PM8150/PON/GPIO button providers.
- Qualify one additional CPU before attempting alioth's full topology.
- Implement alioth DWC3 wrapper, GDSC, clocks, PHY and USB role handling.
- Replace Sweet persistent-log placement with a non-destructive alioth
  debug channel.

## Ordered intake plan

1. UFS root device contract.
   Keep alioth NativeUfs transport and SMMU reuse. Add a whole-`userdata`
   `IOBlockStorageDevice` with 512-byte sectors, bounded reads/writes,
   synchronize-cache and `ufsroot0` root registration.
2. HFS root integration.
   Port the pinned Apple HFS source, built-in registration, `rootrw=1`,
   atomic rename and hard-link behavior. Validate a journaled image and do
   not generalize the dirty non-journaled-root exception.
3. IOSurface/IOMFB service.
   Port the ABI layer, then connect it to `NativeLegacyDisplay`/GOP. Publish
   a default surface, support swap begin/end/wait, vsync and logical power.
4. HID provider layer.
   Port the IOHID event-service ABI and edge-tip behavior. Implement a
   FocalTech GENI-SPI client and alioth PMIC/GPIO buttons.
5. SMP.
   Qualify boot CPU plus CPU 1. Require PSCI success, `initCPU`, timer and
   SGI/IPI evidence on hardware before adding more cores.
6. USB and optional live logging.
   Reuse the CDC-ECM protocol only after alioth DWC3/PHY/power is observed
   and configured correctly. Do not make it a root-mount prerequisite.

## Do not intake as-is

- Sweet `/sweet-ufs` controller takeover, clock/reset sequence and fixed DMA
  layout.
- Sweet persistent log at the tail of `userdata`.
- Sweet `AppleCLCD` direct-copy-to-fixed-panel backend as an alioth display
  driver.
- Goodix GT9896 GENI-I2C, Sweet GPIO numbers and PM6150/PM6150L button
  assumptions.
- Sweet SM7150 eight-core topology and core mask.
- Sweet QSCRATCH, QUSB2 base `0x088e2000`, SS-PHY and clock/GDSC
  programming.
- A claim that `fastboot OKAY`, "IOService registered", "PSCI returned 0" or
  "driver compiled" is functional alioth acceptance.

## Qualification gates

| Area | Minimum passing evidence |
| --- | --- |
| UFS block device | GPT-by-name userdata selection, exact partition geometry, 512-byte sector reads, no out-of-partition write, successful SYNCHRONIZE CACHE |
| HFS root | Kernel mounts the intended journaled HFS+ volume read-write, writes and re-reads files, Foundation rename/exchange path succeeds, cold boot replays or preserves the journal correctly |
| Display | Exactly one expected IOMobileFramebuffer service, valid default surface, successful swap retire, visible frame, vsync delivery, power-state transition |
| Touch | Correct corner mapping and 10-contact tracking within 1125x2436 logical coordinates; edge contacts carry edge-tip for their lifetime |
| Buttons | Power, volume-up and volume-down report the expected HID usages independently of touch |
| SMP | One secondary enters `initCPU`, takes timer and SGI/IPI, survives cold boot; widen topology only after that |
| USB | Host enumerates the alioth device, link state remains stable, and the selected transport does not write storage |

## Top blockers

1. alioth NativeUfs is read-only and manifest-oriented; it does not yet
   provide the writable whole-`userdata` `ufsroot0` required by the HFS route.
2. There is no qualified alioth Darwin 22 IOSurface/IOMobileFramebuffer
   service; the local display classes are foundation/prototype code.
3. FocalTech GENI-SPI and alioth PMIC/GPIO input providers are not wired;
   Sweet Goodix/SPMI code cannot be reused directly.
4. SMP is deliberately disabled and still controlled by sheng-named knobs;
   the Darwin 22 IOCPU/PSCI ABI must be qualified on alioth.
5. Sweet DWC3/QSCRATCH/PHY programming has no alioth evidence, so USB remains
   a later debug task.

