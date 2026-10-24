# alioth reboot reason — where it lives

Two independent cells record why the phone restarted. Both are needed: the
bootloader reads one, the PMIC keeps the other across a power drop.

Sources: `uefi-alioth/Resources/DTBs/alioth.dts` (this phone's own device tree,
the same one in the boot image), and the vendor driver the DT is written for,
`drivers/input/misc/qpnp-power-on.c` in LineageOS
`android_kernel_qcom_sm8250` (lineage-20).

## 1. PM8150 PON block (SPMI, survives the power drop)

```
pm8150@0  (SPMI SID 0)
  qcom,power-on@800   reg = <0x800 0x100>
```

So the PON block is SPMI address `0x800..0x8ff` on SID 0, i.e. PPID
`(0 << 8) | 0x08 = 0x08`.

| Register | Offset in block | SPMI address | Use |
|:--|:--|:--|:--|
| `QPNP_PON_PERPH_SUBTYPE` | `0x05` | `0x805` | `0x04`/`0x05` = GEN2 |
| `QPNP_PON_REASON1` | `0x08` (GEN2 `0xC0`) | `0x808` | why it powered on |
| `QPNP_PON_SOFT_RESET_REASON1` | `0x0E` (GEN2 `0xCB`) | `0x80E` | last soft reset |
| **`QPNP_PON_SOFT_RB_SPARE`** | **`0x8F`** | **`0x88F`** | **restart reason we write** |
| `QPNP_PON_SEC_ACCESS` | `0xD0` | `0x8D0` | unlock, `0xA5`; not needed for `0x8F` |

`qpnp_pon_set_restart_reason()` is the reference:

```c
qpnp_pon_masked_write(pon, QPNP_PON_SOFT_RB_SPARE(pon), GENMASK(7, 1), (reason << 1));  /* GEN2 */
qpnp_pon_masked_write(pon, QPNP_PON_SOFT_RB_SPARE(pon), GENMASK(7, 2), (reason << 2));  /* older */
```

Reason codes (`enum pon_restart_reason` in `include/linux/input/qpnp-power-on.h`):

| Name | Value |
|:--|:--|
| `PON_RESTART_REASON_UNKNOWN` | `0x00` |
| `PON_RESTART_REASON_RECOVERY` | `0x01` |
| **`PON_RESTART_REASON_BOOTLOADER`** | **`0x02`** |
| `PON_RESTART_REASON_RTC` | `0x03` |
| `PON_RESTART_REASON_DMVERITY_CORRUPTED` | `0x04` |
| `PON_RESTART_REASON_DMVERITY_ENFORCE` | `0x05` |
| `PON_RESTART_REASON_KEYS_CLEAR` | `0x06` |

So on this phone: subtype read from `0x805`; write `0x02 << 1 = 0x04` into bits
7:1 of `0x88F` if GEN2, else `0x02 << 2 = 0x08` into bits 7:2.

## 2. IMEM restart_reason (what ABL actually checks)

```
qcom,msm-imem@146bf000
  restart_reason@65c   reg = <0x65c 0x04>
```

Physical address **`0x146bf65c`**, one 32-bit word.

`msm_restart_prepare()` in `drivers/power/reset/msm-poweroff.c` writes
`0x77665500` for bootloader and `0x77665501` as the fallback, then also calls
`qpnp_pon_set_restart_reason()`.

**Discrepancy to keep in mind:** `ios-sweet/shim/shim.c:15` defines
`IMEM_RESTART_REASON 0x146aa65c`, which is base `0x146aa000` — the base used by
`renoir.dts` and `a52q.dts`, not alioth. alioth's own DT says `0x146bf000`.
The kernel must use `0x146bf65c`. Whether the shim's value was ever effective
on this phone is not established; do not reuse it.

## 3. Reset trigger

```
restart@c264000   compatible = "qcom,pshold"
                  reg = <0xc264000 0x04 0x1fd3000 0x04>
```

PS_HOLD register at **`0x0c264000`**; writing `0` drops the hold and the PMIC
resets. The boot shim already does exactly this after an SMC
`PSCI_SYSTEM_RESET` (`shim.c:109-113`), and that path is proven on this phone.

Order used by the kernel driver: write the two reason cells, then
`PSCI_SYSTEM_RESET` over SMC, then `PS_HOLD = 0` if the SMC returns.

## 4. Watchdog fallback

```
qcom,wdt@17c10000   compatible = "qcom,msm-watchdog"
```

APSS watchdog at `0x17c10000`. If the SPMI write path fails, a hung CPU with
the watchdog armed resets the phone without touching any partition. It does not
guarantee the target is fastboot, because no reason cell was written.

## Verified on the device (2026-10-24, image v36)

```
SWEET BATTERY: SPMI arb=0x50010000 APIDs=512, reads via obs, write lane lazy
SWEET AUTORESET: PON subtype 0x4 shift 1 spare 0 -> 0x4 want 0x4 ok
SWEET AUTORESET: IMEM 0x146bf65c <- 0x77665500 read back 0x77665500
SWEET AUTORESET: 90 s elapsed, resetting
```

- SID 0, `SOFT_RB_SPARE` `0x88f`, GEN2 shift 1, reason `0x02` -> cell `0x04`.
  Matches `qpnp_pon_set_restart_reason` exactly.
- IMEM `0x146bf65c` is the right cell for this board (`0x77665500` reads back).
- The phone returns to **fastboot**, not TWRP: the bootloader consumed the
  reason.
- `SEC_ACCESS` (`0x8d0 <- 0xA5`) is **not** needed for this register.
- The write must go through `chnls` at `SP_CHNL_PA + 0x10000 * apid`. Map one
  lane, not the window: the window is 32 MiB and `io_submap` is 8 MiB on arm.
