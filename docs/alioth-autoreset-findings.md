# alioth autoreset — what v32 to v34 established

`SWEET AUTORESET` is a boot argument (`sweet-autoreset=<seconds>`) that makes the
kernel record a restart reason and reset the phone on its own, so an unattended
test does not need a human to press keys.

## Proven

- The thread arms, sleeps, and fires on time. v33 log: `SWEET AUTORESET: armed
  for 90 s` at boot, then reset. Device came back 130 s later.
- `sweet-autoreset` parses, and the flush-before-reset path reaches the medium:
  the log pulled after the reset ends at the reset line, so the flush is the
  last thing that happens.
- IMEM at `0x146bf65c` is writable from the kernel and reads back what was
  written: `SWEET AUTORESET: IMEM 0x146bf65c <- 0x77665500 read back 0x77665500`.
  This also confirms the cell address, against `shim.c`'s wrong `0x146aa65c`.
- SPMI *reads* work: `PMIC@SID0 Power-on reason` is readable, subtype reads
  `0x04` (GEN2), and the spare cell reads `0`.

## Solved: the PON restart reason lands (v36)

v36 log:

```
SWEET BATTERY: SPMI arb=0x50010000 APIDs=512, reads via obs, write lane lazy
SWEET AUTORESET: PON subtype 0x4 shift 1 spare 0 -> 0x4 want 0x4 ok
SWEET AUTORESET: IMEM 0x146bf65c <- 0x77665500 read back 0x77665500
SWEET AUTORESET: 90 s elapsed, resetting
```

Two independent confirmations: the cell reads back `0x4`, and the phone comes
back to **fastboot** instead of TWRP. v32/v33/v34 all returned to TWRP, so the
bootloader really did read the reason.

The one change from v34 was the chnls mapping. v34's `ml_io_map(SP_CHNL_PA,
0x2000000)` panicked in `kmem_alloc` with `KERN_NO_SPACE` before any SPMI write
ran: `io_submap` is `IO_MAP_SIZE = 8 MiB` on arm (`osfmk/arm/io_map.c:65`) and
the chnls window is 32 MiB. v36 maps one 64 KiB lane at
`SP_CHNL_PA + 0x10000 * apid` when the APID is first known. That is the whole
fix.

## The earlier failure, kept for the record

The masked write reports success and the cell reads back unchanged. The first
attempt issued `EXT_WRITEL` on the observer channel (`obsrvr`, `0x80` per APID);
the vendor driver writes on the read/write channel (`chnls`, `0x10000` per APID)
and only reads on the observer. v34 corrected the base, and the cell still reads
back `0`.

Neither of the two candidates I listed was the cause. The APID is owned by EE 0
and `SEC_ACCESS` is not needed: the write channel simply was never reached,
because the mapping that precedes it panicked the kernel.

## Device state

The v34 boot (10:04) went dark because it panicked, not because ABL wedged. The
user's photo of the screen showed the panic; the cmdline on it read
`sweetlog=1 ... sweet-autoreset=90`, matching v34. Recovery needed the physical
keys.

v36 boots, reaches the Apple logo, holds the lock screen for 90 s, then resets
into fastboot on its own. Nothing has been written to any partition in this
sequence; every boot has been `fastboot boot`.

## Still open

- The 09:55 v34 log shows `observer reads only`, which is the v33 string: that
  log was pulled before the 10:03 chnls build. Do not read it as evidence about
  the chnls path.
- `sweet-log-pull.sh` used `stat -c`, which macOS `stat` rejects. Fixed to
  `stat -f %z`; the pull worked either way because the size is only printed.
