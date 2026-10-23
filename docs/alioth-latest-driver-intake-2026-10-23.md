# alioth latest driver-intake audit (2026-10-23)

Audit time: `2026-10-23T07:24:55+08:00`

Scope: read-only comparison of the latest relevant upstream repositories
against the snapshots in:

- `docs/alioth-ios-sweet-driver-matrix.md`
- `docs/alioth-driver-port-backlog.md`

The audit used `git ls-remote`, filtered temporary clones under `/tmp`, and
commit-to-commit source inspection. It did not update a submodule, edit
existing code or documentation, access a device, run `fastboot`, or commit
anything.

## Verdict

The current alioth root-first priority does not change.

- The only new top-level commits after the previous audit snapshots are
  `UniXNU/UniXNU` `261d7541...`, which advances only the
  `NativeMobileDisplay` submodule, and two `UniXNU/UniXNU-sheng` commits.
- The display pointer exposes two post-audit-snapshot driver commits:
  fixed-refresh reporting and early retirement of a GPU-copied swap.
- The sheng commits add a board-specific CPU0 parking policy and restrict its
  secondary CPUs to the boot core plus the five big cores.
- There are no new commits in `UniXNU/NativeUfs`, either
  `Allenwdk/UniXNU-uke` branch, `wochgn/UniXNU-guacamole`,
  `dipa-studio/ios-sweet`, or `xVanTuring/UniXNU-pipa`.
- The local core tree is behind upstream on `NativeMobileDisplay` and
  `NativeHidInput`. The HID lag contains several touch correctness fixes that
  matter for alioth's FocalTech path, but those fixes are still outside the
  first-root critical path.
- None of the newly exposed upstream changes is required to mount the first
  HFS+ root. The first-root gate remains the whole-`userdata` NativeUfs/HFS
  contract already recorded in the existing plan.

## Remote snapshot

The previous audit baseline is local documentation commit
`1e5fdc6293ff075ecf09156699dce2d1bb7e25d4`.

| Repository and ref | Previous snapshot | Current remote SHA | Delta |
| --- | --- | --- | --- |
| `UniXNU/UniXNU` `main` | `8a6672f7fa4ad6c41f9c5cca32992bc618752638` | `261d7541fbd17daeb8bdfb06783ca181464b6810` | One commit; display submodule pointer only |
| `UniXNU/NativeUfs` `main` | `af785c1777161a62931425c2faaa536ce0bebd5b` | `af785c1777161a62931425c2faaa536ce0bebd5b` | None |
| `Allenwdk/UniXNU-uke` `main` | `0fde2b1b53b871e9b33b1b242a7034f56e2c71c4` | `0fde2b1b53b871e9b33b1b242a7034f56e2c71c4` | None |
| `Allenwdk/UniXNU-uke` `uke-usbserial` | `ce8498a06d6737d0558521a8635b8f5dfb417e11` | `ce8498a06d6737d0558521a8635b8f5dfb417e11` | None |
| `wochgn/UniXNU-guacamole` `main` | `6269d315dbf43c4f7334177721f5c18e9b633f97` | `6269d315dbf43c4f7334177721f5c18e9b633f97` | None |
| `UniXNU/UniXNU-sheng` `main` | `9c88b90155ea5e400f5f75d06104dee96260619a` | `251b291adf941b9953fe3ae66a808b9b626e164d` | Two commits; SMP scheduling and board CPU mask |
| `dipa-studio/ios-sweet` `main` | `067c9b3d463ecfbe37520683637ea2e6af31f3a4` | `067c9b3d463ecfbe37520683637ea2e6af31f3a4` | None |
| `xVanTuring/UniXNU-pipa` `main` | `6749f2fb443f81e64f76bf28406265b8e8c5d0e2` | `6749f2fb443f81e64f76bf28406265b8e8c5d0e2` | None |

The relevant upstream submodule change is:

| Module | Previous parent pin | Current parent pin | Relevant commits |
| --- | --- | --- | --- |
| `UniXNU/NativeMobileDisplay` | `c031d882b9721d943383d11f66dc4748252ce929` | `8666a9950c9699b368931d18cac92c0fdb2ce9c0` | `c99046af...`, `8666a995...` |

Other relevant submodule heads still match the current upstream pins:
`ArmCpu` `1951cc8444b340d7d8879e9418220cf29eb711d7`,
`NativeGeniSpi` `b5399b5646c4d7415deec6d887fd51715a97a024`,
`DarwinBoot` `d099354d0eab34df5088ca1ba89c9c88bc3260bb`,
`NativeKeyStoreEvents` `e59bac4ad1ac8825919ae94da8440a64d8ab90b3`,
`NativeAdreno` `b686b9054f57d393c80195b82568a09f8ca106a2`,
and `NativeMetal` `eed65758722bd4a1219de5161a8ed9ef0bc547da`.

## New display changes

### 1. Fixed-refresh reporting

Source:

- `UniXNU/UniXNU` `261d7541...`
- `UniXNU/NativeMobileDisplay` `c99046af4ac1cecfcef2575bb0025d2e21e005a4`
- `NativeLegacyDisplay.cpp`, `getRefreshAttributes`

Change:

- `sheng-vsync=<Hz>` already replaced the minimum refresh interval.
- `sheng-vsync-fixed=1` now also makes the maximum interval equal to the
  minimum, so the render server sees one fixed rate instead of a 24 Hz to
  requested-Hz range.

Portable contract:

- The `IOMobileFramebuffer` refresh-attribute semantics and one-rate panel
  policy are portable.
- No alioth display controller or panel register is involved.

Why it may apply to alioth:

- It can stop render-server pacing from choosing a fictional refresh rate
  between GOP copy rates.
- It is useful only after an alioth IOMFB/default-surface service exists.

alioth replacement:

- Report the measured alioth GOP presentation cadence, not an assumed K11A
  DSI rate.
- Keep the 30 Hz CPU-present path separate: the new code intentionally ignores
  `sheng-vsync` values at or below 60 Hz.
- Prefer a board-neutral boot-argument alias if this is integrated into the
  alioth module instead of retaining only the `sheng-*` spelling.

Risk:

- A false fixed rate can make CoreAnimation pace faster than the real scanout
  copy and create backlog.
- This changes only advertised timing; it does not create vblank or improve a
  slow copy backend.

First-root critical path: No.

### 2. Early retirement after a GPU client takes a swap

Source:

- `UniXNU/NativeMobileDisplay` `8666a9950c9699b368931d18cac92c0fdb2ce9c0`
- `NativeLegacyDisplay.cpp`, `kickOnce` and `finishGpuSwap`
- `NativeGOPPresenter.cpp`, `offer` and `completeSwap`

Change:

- A GPU hand-off frame now retires as soon as the privileged GPU client
  accepts it.
- The display driver keeps the `transferred` latch until the copy completes,
  so the next frame is still held back.
- A failed early-retired copy loses that frame, increments a counter, and
  switches the next frame to the CPU fallback.
- `sheng-early-retire=0` restores retirement after copy completion.

Portable contract:

- The swap lifetime rule, one offered frame at a time, retained source memory,
  completion callback, and fallback policy are portable.
- The GOP memory mapping and boot arguments are integration details, not
  alioth hardware facts.

Why it may apply to alioth:

- Once alioth has a working GPU client, it can overlap compositor rendering
  with the GPU copy into scanout.
- The commit reports useful measurements on sheng: compositor render and copy
  were serialized before this change.

alioth replacement:

- Keep the existing CPU GOP path as the default until the GPU client is
  independently qualified.
- Replace the sheng GOP/GPU wiring with alioth's scanout mapping and client,
  retaining the same one-frame ownership rules.
- Require fault-injection tests for failed copy, abandoned client, process
  exit, and multiple pending swaps.

Risk:

- `completeCurrentSwap()` is called before the GPU copy physically finishes.
  This is safe only under the documented assumptions: the presenter retains
  the frame and the `transferred` latch prevents reuse.
- A client that interprets swap retirement as scanout completion could observe
  a semantic change. Keep the escape argument and do not enable this by
  default until alioth graphics tests cover it.

First-root critical path: No.

## New HID, button, and touch changes available but not locally pinned

These are not top-level upstream commits after the previous audit SHA. They are
nevertheless important intake because the local core tree pins
`NativeHidInput` `588c96fe3b1bf079d4d9a9f1847456d2c9e76793`, while current
upstream pins `ba03e5d147452b2166aab7435d219fa10877672b`.

### 3. Hardware button sink and four-bit consumer report

Source:

- `NativeHidInput` `28f9b463b48e862f2def91d29c11b9143785715e`
- `NativeHidInput.cpp`, report descriptor, `buttonsNotify`,
  `buttonsAction`, and start/stop registration

Change:

- Report 2 gains Power, Volume Increment, and Volume Decrement beside the
  synthetic Menu/Home bit.
- A board driver can register through the weak `native_buttons_set_sink`
  contract and reports an entire pressed-bit mask on every change.
- Updates are dispatched on the HID work loop.

Portable contract:

- The weak sink and level-transition model are portable.
- The HID consumer usage mapping is an IOKit contract, not a PMIC driver.

Why it may apply to alioth:

- alioth's future PM8150/PON/GPIO driver can publish Power and both volume
  buttons into this sink.
- It avoids polling or guessing a Home button on a device whose userland is
  the no-Home-button iPhone X.

alioth replacement:

- Supply alioth PM8150 PON and GPIO level transitions.
- Treat `RESIN` as volume-down only after alioth readback, as already required
  by the existing backlog.
- Keep button discovery and masking alioth-specific.

Risk:

- The sink masks hardware bits with `0xe`; the Menu/Home bit remains synthetic.
  If alioth later needs a physical Home-equivalent, that requires an explicit
  mapping change rather than assuming the sink accepts it.
- Incorrect held-state handling can leave a consumer key stuck.

First-root critical path: No.

### 4. Ten-finger digitizer correctness fixes

Source commits:

- `22f2dd454e8742f0639a737065f59ab42927a165`:
  report every finger as a confident finger, not a palm.
- `e43c2541133e25bdb48df564a4283888b64cc231`:
  count multi-finger reports and lifts of vanished fingers.
- `75efe1c5ee32b2da3ef8dc978adbdc683eef11a8`:
  lift vanished fingers by identifier instead of by producer slot.
- `ba03e5d147452b2166aab7435d219fa10877672b`:
  dispatch each report's fingers as one digitizer collection.

Relevant code:

- `NativeHidInput.cpp`, touch report descriptor and `touchSend`
- `NativeHidInput.cpp`, `DigitizerCollectionDispatch` property
- `NativeHidInput.cpp`, edge-tip vtable wrapper

Why it may apply to alioth:

- alioth touch is a 10-contact FocalTech device. These changes address exactly
  the failures a 10-contact producer exposes:
  palm classification, stuck contacts after slot compaction, and per-finger
  event dispatch that prevents UIKit from seeing a multi-finger set.
- Setting `DigitizerCollectionDispatch` is necessary for a report's fingers
  to arrive as one collection.

alioth replacement:

- Keep the HID contract and transport-neutral touch sink.
- Replace the producer with the alioth FocalTech over GENI-SPI implementation.
- Parameterize normalized axes. Current source assumes `30480 x 20320`; alioth
  reports raw `0x21c0 x 0x4b00` (`8640 x 19200`).
- Recompute edge-tip distance from the alioth panel and the iPhone X logical
  1125x2436 coordinate mapping.
- Audit the `IOHIDEventDriver` private vtable slot and object-offset wrapper
  against the actual iOS 16.7.16 IOHIDFamily before enabling edge-tip.

Risk:

- `kDispatch=198` and the related event slots are version-specific private ABI,
  not a stable public contract. A mismatch can corrupt dispatch or crash
  backboardd.
- Producing the correct report shape does not prove that `IOHIDEventDriver`,
  SpringBoard, and UIKit accept the device.
- Touch is not useful until display and button behavior can be observed, so
  this must not displace the root-storage gate.

First-root critical path: No.

## New SMP and scheduling changes

### 5. sheng CPU0 parking, CPU selection, and per-core attribution

Sources:

- `UniXNU/UniXNU-sheng`
  `19960fa2d583c5f31d1d9c5b8bfc98d9e02f37ea`
- `UniXNU/UniXNU-sheng`
  `251b291adf941b9953fe3ae66a808b9b626e164d`
- `package/BootProgress.c`, `cpu0_park`, sampling counters, and profile output
- `board.toml`, `cpu_regs`

Change:

- A lowest-priority kernel thread is bound to `master_processor` and yields
  forever, keeping sheng's little boot core occupied when the scheduler would
  otherwise place work there first.
- sheng now starts the boot core plus `0x300`, `0x400`, `0x500`, `0x600`, and
  `0x700`; it leaves its other two little cores off.
- The profile records interrupted samples per core and process and prints the
  distribution for the busiest processes.

Classification:

- CPU0 parking and the CPU mask are sheng board policy, not portable alioth
  hardware code.
- Per-core sample attribution is a portable diagnostic idea.

alioth facts:

- alioth CPU `0x000..0x300` reports capacity `0x400`.
- CPUs `0x400..0x600` report capacity `0x766`.
- CPU `0x700` also reports capacity `0x766` but has a separate frequency
  domain and higher dynamic-power coefficient.
- The current alioth ADT deliberately exports only CPU0 and the kernel uses
  `cpus=1`.

alioth replacement:

- Do not copy sheng's CPU mask or enable the parking thread while alioth is
  single-core.
- First prove one secondary CPU through PSCI, kernel entry, timer, and SGI/IPI.
  An alioth high-capacity MPIDR such as `0x400` is a candidate for that single
  test, not a policy copied from sheng.
- After SMP and scheduler qualification, port the per-core sampling rows first.
  Evaluate boot-core parking only as an alioth benchmark variable.

Risk:

- A permanently runnable lowest-priority thread can hide scheduler topology
  mistakes, consume power, distort thermal results, and delay work if priority
  or affinity semantics differ.
- The sheng mask is invalid for alioth's 4+3+1 topology.
- The commit-message 40-to-55 fps result is sheng evidence, not alioth proof.

First-root critical path: No.

## What did not change

| Area | Latest result | Consequence for alioth |
| --- | --- | --- |
| `NativeUfs` | Same `af785c17...` as the previous audit | Keep the existing alioth fork at `6871d646...`; it is a descendant of `af785c17...` with alioth probe evidence. Upstream still does not provide a general whole-`userdata` HFS root |
| `ios-sweet` HFS/UFS patches | Same `067c9b3d...` | No new root or HFS contract. Do not change the root-image plan based on this audit |
| `UniXNU-uke` USB serial | Same `ce8498a0...` | The mechanics/descriptors remain useful; alioth DWC3 wrapper, clocks, PHY, and SMMU data are still alioth work |
| `UniXNU-uke` main | Same `0fde2b1b...` | Its Gunyah watchdog, high-DRAM, and scheduler work remains uke/platform-specific; do not import it into alioth as generic SMP |
| `UniXNU-guacamole` | Same `6269d315...` | Still a userland integration reference, not a new alioth driver source |
| `UniXNU-pipa` | Same `6749f2fb...` | No general driver tree to intake; ADT/UEFI context only |
| `NativeGeniSpi` | Same pinned `b5399b56...` | The controller exists, but the alioth FocalTech protocol client is still missing |

Upstream `NativeUfs` does contain constrained writes for sheng's Data-store,
log, and export windows. Those are not a replacement for a bounded
whole-`userdata` block device:

- They target reserved windows at the end of `userdata`.
- The root publication still expects a `SHNGROOT` manifest.
- The chosen alioth route uses journaled HFS+ over the whole partition with
  `rd=ufsroot0 ufsrw=1 rootrw=1`.

Do not enable `sheng-log` or its end-of-`userdata` reserve on alioth.

## Local pin follow-through

| Module | Local pin | Current upstream pin | Action |
| --- | --- | --- | --- |
| `NativeMobileDisplay` | `c031d882...` | `8666a995...` | After the root proof, review and apply the two display commits. Their code changes are confined to `NativeLegacyDisplay.cpp`; they introduce no new board MMIO |
| `NativeHidInput` | `588c96fe...` | `ba03e5d1...` | Before alioth button/touch publication, integrate the button sink and four touch-correctness commits. Re-audit the private HID ABI against iOS 16.7.16 first |
| `NativeUfs` | `6871d646...` on the alioth probe branch | `af785c17...` | Keep the alioth branch. It descends from current upstream main and adds only the alioth stream/GPT/probe work; do not discard it for a plain upstream checkout |
| `ArmCpu` | `1951cc84...` | `1951cc84...` | No module update required for the audit. SMP remains blocked on alioth integration and hardware evidence |
| `NativeGeniSpi` | `b5399b56...` | `b5399b56...` | No module update required. Missing work is the alioth SE/pin/IRQ description and FTS protocol |

The local core tree already has a separate mechanics-only NativeUsbDevice batch
in its own history. That work is not part of these upstream deltas and remains
subject to the existing USB milestone: host-tested rings and descriptors are
not CDC-ACM enumeration or a live logging pass.

## Ordered intake plan

The active root sequence remains unchanged:

1. Finish the reviewed journaled HFS+ image and its hashes.
2. Publish GPT `userdata` as a bounded, writable `ufsroot0` through the alioth
   NativeUfs fork.
3. Prove a bounded read/write/cache round trip and an HFS+ cold-boot mount.
4. Only after root and launchd evidence, continue with USB logging, secondary
   CPU, buttons, display, and touch in the order already defined by
   `docs/alioth-driver-port-backlog.md`.

When those later milestones begin:

1. For display, first keep the current CPU/GOP presentation contract. Then
   import fixed-refresh reporting. Enable early retirement only after a GPU
   client and copy-failure tests exist.
2. For buttons and touch, sync `NativeHidInput`, then parameterize alioth axes,
   edge thresholds, and the PM8150 button provider.
3. For SMP, add one secondary CPU and per-core diagnostics before considering
   any boot-core parking or reduced CPU mask.
4. For USB, continue the existing gated CDC-ACM work; do not import sheng or
   uke wrapper/PHY constants.

## Classification summary

Portable Darwin/IOKit contracts:

- IOMFB refresh intervals and swap lifetime semantics.
- HID consumer-button level transitions and 10-finger digitizer report shape.
- `DigitizerCollectionDispatch` and stable contact identifiers.
- Per-core sample attribution.
- The existing BSD `ufsroot0`, bounded write, and journaled-HFS semantics from
  the current plan.

Board-specific hardware or policy:

- sheng CPU0 parking and sheng's five-big-core mask.
- sheng GOP/GPU lease layout, copy timing, and `sheng-*` arguments.
- any fixed display rate not measured on alioth.
- PM8150/PON/GPIO APIDs, pins, polarity, and interrupt routing.
- FocalTech GENI-SPI clocks, reset/IRQ GPIOs, raw axes, and edge thresholds.
- DWC3 wrapper, GDDR/GCC clocks, PHY, QSCRATCH, VBUS, and SMMU stream data.

Not adoptable:

- sheng's CPU mask as an alioth topology.
- CPU0 parking as a default performance fix.
- early GPU swap retirement without an alioth GPU client and failure tests.
- private IOHID vtable offsets without checking the iOS 16.7.16 IOHIDFamily.
- sheng touch axes or Goodix transport as alioth FocalTech.
- sheng end-of-`userdata` logs, window writes, or `SHNGROOT` publication as a
  substitute for the selected whole-partition HFS+ root.
- uke Gunyah watchdog/high-DRAM policy as generic alioth SMP work.

## Five most important intake items

1. Integrate `NativeHidInput` `ba03e5d1...` before alioth touch: one digitizer
   collection, valid/confident fingers, and identifier-based lifts are direct
   prerequisites for usable 10-contact FTS behavior.
2. Integrate the `28f9b463...` button sink contract before the PM8150 button
   milestone; it gives a clean board-driver-to-HID boundary.
3. Take fixed-refresh reporting from `NativeMobileDisplay`
   `c99046af...` when the alioth IOMFB/GOP service is ready, using measured
   alioth cadence rather than assumed panel timing.
4. Treat early swap retirement from `8666a995...` as an optional GPU
   performance change with strict ownership and failure tests, not as a root
   or initial-display requirement.
5. Port only the per-core attribution idea from sheng's
   `19960fa2...`/`251b291a...` work; do not copy the CPU mask or default CPU0
   parking policy.

## Checked SHA appendix

Top-level branches:

```text
UniXNU/UniXNU main                       261d7541fbd17daeb8bdfb06783ca181464b6810
UniXNU/NativeUfs main                    af785c1777161a62931425c2faaa536ce0bebd5b
Allenwdk/UniXNU-uke main                 0fde2b1b53b871e9b33b1b242a7034f56e2c71c4
Allenwdk/UniXNU-uke uke-usbserial        ce8498a06d6737d0558521a8635b8f5dfb417e11
wochgn/UniXNU-guacamole main             6269d315dbf43c4f7334177721f5c18e9b633f97
UniXNU/UniXNU-sheng main                 251b291adf941b9953fe3ae66a808b9b626e164d
dipa-studio/ios-sweet main               067c9b3d463ecfbe37520683637ea2e6af31f3a4
xVanTuring/UniXNU-pipa main              6749f2fb443f81e64f76bf28406265b8e8c5d0e2
```

Relevant module heads:

```text
UniXNU/NativeMobileDisplay main          8666a9950c9699b368931d18cac92c0fdb2ce9c0
UniXNU/NativeHidInput main               ba03e5d147452b2166aab7435d219fa10877672b
UniXNU/ArmCpu main                       1951cc8444b340d7d8879e9418220cf29eb711d7
UniXNU/NativeGeniSpi main                b5399b5646c4d7415deec6d887fd51715a97a024
UniXNU/DarwinBoot main                   d099354d0eab34df5088ca1ba89c9c88bc3260bb
UniXNU/NativeKeyStoreEvents main         e59bac4ad1ac8825919ae94da8440a64d8ab90b3
UniXNU/NativeAdreno main                 b686b9054f57d393c80195b82568a09f8ca106a2
UniXNU/NativeMetal main                  eed65758722bd4a1219de5161a8ed9ef0bc547da
UniXNU/NativeNovatekTouch main           717c866fca652407117447df7a7c7d5addc7bfc6
```

Local state used for drift comparison:

```text
UniXNU core worktree HEAD                91a66dd6cc237291674cf10c6a353e4b971e80aa
NativeMobileDisplay local pin            c031d882b9721d943383d11f66dc4748252ce929
NativeHidInput local pin                 588c96fe3b1bf079d4d9a9f1847456d2c9e76793
NativeUfs local alioth branch            6871d6467e6d98dfef407de2485438d121b0000b
NativeGeniSpi local pin                  b5399b5646c4d7415deec6d887fd51715a97a024
ArmCpu local pin                         1951cc8444b340d7d8879e9418220cf29eb711d7
```
