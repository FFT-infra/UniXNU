# NativeButtons alioth discovery layer

This directory is an isolated, host-tested design layer for alioth buttons. It
does not map MMIO, start an interrupt thread, or access a device.

The implementation deliberately separates:

1. `AliothButtonFacts.h`: facts taken from the alioth DTS.
2. `SpmIDiscovery.h`: observations, unique-APID resolution, explicit blockers,
   and a plan containing only read operations.
3. `ButtonEventLogic.h`: debouncing and held-button behavior.

## Locked alioth facts

Source: `uefi-alioth/Resources/DTBs/alioth.dts` and its matching `alioth.dtb`.

| Fact | Value | DTS lines |
| --- | --- | --- |
| SPMI execution environment | `0` | 4172-4179 |
| PM8150 SPMI slave ID | `0` | 4187-4189 |
| PON register base / size | `0x800` / `0x100` | 4193-4195 |
| PON SPMI peripheral ID | `0x08` | 4196-4197 |
| KPDPWR / RESIN | power / volume-down | 4196-4218 |
| PM8150 GPIO controller | base `0xc000`, size `0xa00` | 4259-4262 |
| volume-up GPIO | GPIO6, SPMI peripheral ID `0xc5` | 4262-4263, 4319-4328 |
| volume-up polarity | active-low | 39122-39126 |
| volume-up debounce | `15 ms` | 39128 |
| PDC controller | base `0xb220000`, size `0x30000` | 3691-3699 |

The PON debounce property is `0x3d09`, i.e. `15625 us`. The volume-up Linux
key code is `0x73`; KPDPWR is `0x74` and RESIN is `0x72`.

PDC pin and IRQ are intentionally absent. The alioth DTS has a raw controller
link, but this layer does not turn that link into a pin or platform IRQ. No
value is inherited from UniXNU-sheng.

## Discovery contract

- APIDs must come from a complete runtime APID-map snapshot.
- A matching APID must be the only match for the requested PPID, must have
  the IRQ-owner bit set, and must be owned by EE 0.
- Zero matches return `NotFound`; multiple matches return `Ambiguous`.
- A missing, partial, or non-runtime map returns `NotScanned` or an explicit
  scan blocker.
- PDC route discovery must be recorded as runtime evidence. A default-created
  route is unresolved and blocks the report.
- `makeReadOnlyPlan()` can emit only reads for arbiter identity, APID owner/map
  words, PON/GPIO real-time status, and PDC version. There is no write action.

## Minimum real-device readback still required

1. Arbiter version at `core + 0x0` and feature APID count at `core + 0x4`;
   require a supported v7 layout and a count in `1..1024`.
2. APID map words at `core + 0x2000 + 4*i` and owner words at `cnfg + 4*i`;
   resolve exactly one EE0 IRQ-owner for PPID `0x08` (PON) and `0xc5` (GPIO6).
3. PDC version at `pdc + 0x1000`, followed by an independently verified,
   exact PDC pin/platform IRQ route. Do not infer these from Sheng.
4. PON real-time status at the resolved APID observer channel offset `0x10`,
   idle and while KPDPWR/RESIN are individually held.
5. GPIO6 real-time status at the resolved APID observer channel offset `0x10`,
   idle and while volume-up is held; confirm the DTS active-low mapping.

## Host tests

```sh
drivers/NativeButtons/tests/run.sh
```

The runner uses Apple Clang when available and compiles with
`-std=c++17 -Wall -Wextra -Werror -pedantic`, then repeats the suite with
AddressSanitizer and UndefinedBehaviorSanitizer.

Architectural references:

- `UniXNU-sheng` `package/ButtonProbe.cpp` at
  `df7b774b629c4ddf57981ff7a695645ed6ae55d3`: interrupt-driven level change,
  storm handling, and APID lookup shape.
- `Allenwdk/UniXNU-uke` `package/*Tests.c`: small deterministic fixtures and
  all-or-nothing refusal checks.

No Sheng APID, PDC pin/IRQ, SPI number, MMIO address, or register write was
copied into this layer.
