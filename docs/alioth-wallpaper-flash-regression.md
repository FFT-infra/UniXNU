# After the wallpaper userdata flash, SpringBoard stops before UIKit

The `fastboot flash userdata` of `ios16-root-20H392-v16-wallpaper.simg`
(2026-10-24 15:16 local, sha256 `aa46e208…`, 15 sparse segments, all OKAY,
289.7 s) put the phone into a state where SpringBoard starts and then blocks.
Everything before SpringBoard is unaffected. Two boots were captured after it,
both with the same outcome.

## The wallpaper edit is ruled out

The discriminating experiment was run: a full-partition flash of
`ios16-root-20H392-v16.simg` — the same image **without** the PNG-for-HEIC
wallpaper edit — 2026-10-24 16:43 local, 15 sparse segments, all OKAY, 290.0 s.
The phone was then booted with `markers-v40b-sample600.img`
(`sweet-autoreset=600 sweetsample=SpringBoard,backboardd`, sha256
`8878e490…`) and the log pulled from recovery.

It fails **identically**:

| | healthy 13:58 | wallpaper 15:39 | no-wallpaper 17:16 |
|:--|--:|--:|--:|
| `SWEET AKS: SpringBoard` | 112 | 4 | 4 |
| `SWEET SURF: SpringBoard` | 14 | 0 | 0 |
| `\|SpringBoard\|` log lines | 388 | 0 | 0 |
| `exec PosterBoard` | yes | no | no |
| SpringBoard threads at t+90 s | 37 | 2 | 3 |

So the PNG edit is not the cause. The whole-partition replace is.

## What still works

- The partition mounts: `hfs: mounted iOSRoot on device b(1, 0)`, journal
  replayed.
- `mobile_obliterator: No obliteration needed`, `Early boot complete`.
- 625 `exec` calls including `runningboardd`, `backboardd`, `lsd`,
  `cfprefsd`, `distnoted`, `securityd`, `keybagd`, `mobile_obliterator`,
  `com.apple.datamigrator` (3×).
- backboardd: full bring-up, `SWEET FB: swap 2/3/4` at 1125x2436, `SWEET SURF:
  backboardd create id 2/3 fmt 0x42475241`, then idle.

The wallpaper assets themselves were never at fault: all three `main.caml` and
`index.xml` pass `xmllint`, all five PNGs decode (`Head Mask` 1113x1170 alpha,
`Head` 1114x1170, `Leg` 1170x1023 alpha, `Arm` 1170x1170 alpha, `Body`
815x1170 alpha), and every `src=` points at a file that exists.

## What stops

SpringBoard reaches exactly four AKS calls and then no more:

```
SWEET AKS: SpringBoard sel 0  -> 0x0
SWEET AKS: SpringBoard sel 35 -> 0x0
SWEET AKS: SpringBoard sel 7  -> 0x0
SWEET AKS: SpringBoard sel 35 -> 0x0
```

In the healthy 13:58 boot the same four are followed by `SWEET SURF:
SpringBoard sel 40`, then 108 more AKS calls, `PosterBoard` launches, 388
SpringBoard log lines, 14 surfaces. In both post-flash boots the four are the
end: **0** `SWEET SURF: SpringBoard`, **0** `|SpringBoard|` log lines, **0**
`PosterBoard`.

The in-between is all other processes: `chronod`, `contextstored`, `securityd`,
`thermalmonitord`, `pkd`, `datamigrator`, `exec xpcproxy` — the boot goes on
without SpringBoard.

## Where it blocks

Sampled at t+90 s, slide `0x14690000` on the no-wallpaper boot, resolved
against 20H392's `dyld_shared_cache_arm64.*`:

```
t0  pc libsystem_kernel.dylib+0x1030
    bt libsystem_kernel+0x12db8, libdispatch+0x36308, libdispatch+0x36694,
       libxpc+0xe0ec, Foundation+0x9a224, Foundation+0x33cb0,
       Foundation+0x326e0, Foundation+0x3261c, CoreServices+0xdcf34,
       CoreServices+0x258d8, CoreServices+0xb58e4, CoreServices+0x1cbc0,
       CoreServices+0xe2238

t1  pc libsystem_kernel.dylib+0x1ad0
t2  pc libsystem_kernel.dylib+0x14f0
```

The main thread is inside a **synchronous XPC round-trip made from CoreServices
through Foundation and libxpc**, three frames short of where the healthy boot
has it. The stack is byte-identical across all four post-flash samples at
t+90/210/330/450 s: it is waiting, not spinning.

In the pre-flash boot the same thread is in `CoreFoundation ← GraphicsServices
← UIKitCore ← SpringBoard`, i.e. the normal run loop, already past startup.

So SpringBoard does not deadlock inside wallpaper decoding, PosterBoard, or the
graphics stack. It blocks waiting for a reply from a peer that never answers,
before UIKit is ever entered.

## What this rules out

- **Not the wallpaper edit.** The no-wallpaper image fails identically.
- **Not pcie.** The control boot has no `pcie=` in its cmdline and 0 `pcie:`
  lines, and fails identically.
- **Not the kernel.** All boots after 15:11 use `kernel.pie.v40.stripped`,
  sha256 `73ce91c1…`; both the healthy pre-flash boot and every failing boot
  link the same object list.
- **Not the wallpaper decode path.** The failure is upstream of it, and the
  PNGs and CAML are valid.
- **Not `datamigrator`.** The pre-flash log execs it 5 times, the post-flash
  logs 3. That is a consequence of the boot dying early, not the cause.

## What is left

The whole `userdata` partition was replaced, so `/private/var` — keybags,
preferences, `db`, `containers`, `/private/var/mobile/Library/Preferences` —
is whatever the image carried, not what the phone had accumulated over the
earlier boots.

`mkb` records a **different user session UUID** than the pre-flash boots:
`DCEFD8C1-6F80-49F2-94B3-D0CDC18DAD61` post-flash against
`AC266E3C-1A0C-4A89-947D-8ECCC80B5019` pre-flash, and the post-flash log says
`UMD_INIT: Regular mode Setup for user:501: Num Sessions:1,
CurrentsessionID:-1` where the healthy one has a populated `_gUserSessionTable`.

The remaining candidate is `/private/var` state the image shipped, against
which some CoreServices peer never answers. The next discriminator is a second
boot of the **same** partition with no flash in between: a first-boot migration
that completed would clear it, a persistent record would not.
