# alioth WiFi and wallpaper — what is actually available

Two of the earlier judgements were wrong. Both features have working reference
implementations. This records what exists, where it lives, and what alioth has
to do differently.

## 1. WiFi

`UniXNU/NativeWifi` exists (2026-10-23, 58 commits). It is **ath12k**: WCN7850
(`17cb:1107`) and QCN9274 (`17cb:1109`). alioth carries QCA6390, `17cb:1101`,
which is **ath11k**. So this is a port, not a reuse.

The parts that transfer unchanged are the board contract and the PCIe bring-up:

```c
struct lx_platform {   /* include/nw_abi.h */
    uint64_t now_ms, sleep_ms;
    void   (*poll)(void);
    void   (*log)(const char *);
    void   (*halt)(void);
    void  *(*alloc)(size_t), (*free)(void *);
    uint64_t (*dma_alloc)(size_t, uint64_t *pa);
    void   (*dma_free)(uint64_t pa, size_t);
    uint64_t (*dma_map)(uint64_t pa, size_t);
    void   (*dma_sync)(uint64_t pa, size_t, int);
    int    (*fw_get)(const char *name, void **data, size_t *len);
};

struct nw_board {
    uint64_t bar, bar_len;
    uint16_t device_id;
    uint32_t msi_cap, irq_base;
    uint32_t (*cfg_read)(uint32_t off);
    void     (*cfg_write)(uint32_t off, uint32_t val);
    void    *(*msi_target)(void);
    uint64_t (*map_phys)(uint64_t pa, size_t len);
};
```

`nw_pci.c` fixes vendor `0x17cb`, takes the device id from the board, and
selects the board by config space `0x2c` (subsystem vendor/device).

Board values alioth needs, measured from this phone's DT:

| Item | Address / value |
|:--|:--|
| PCIe0 parf | `0x1c00000 + 0x3000` |
| DBI | `0x60000000 + 0xf1d` |
| ELBI | `0x60000f20` |
| IATU | `0x60001000` |
| config window | `0x60100000 + 0x100000` |
| GDSC (bit31 PWR_ON) | `0x6b004` |
| votes | `0x52008` / `0x52000` |
| PERST# | GPIO 79 |
| WAKE# | GPIO 81 |
| MSI | `0x17a10040` |
| SPI | `0x300..0x31f` |
| iommu-sid-base | `0x1c00` |

Stage 1 template is sheng's `package/WifiProbe.cpp`: probe the link, read the
device id, stop. Stage 2 is GDSC + the four clocks + DBI link control, then read
`17cb:1101` from config space. **No MHI** until the link is actually up — MHI on
a link that never trained produces a timeout that looks like a firmware fault
and hides the real one.

## 2. Wallpaper

sheng already replaced the default wallpaper's HEIC layers with PNG and mounted
the result over the sealed animation with a union mount. alioth does not need a
union: the root image is assembled from scratch on the host, so the PNGs can
simply be written in place before the image is sealed.

alioth is iPhone10,3 on 20H392. Its default is wallpaper 7400, the iOS 16 WWDC
layered animation:

```
Library/Wallpaper/Collections/iOS_16~iphone.wallpaperCollection/Wallpapers/
  7400.WWDC_2022-375w-812h@3x~iphone.wallpaper/
    7400.WWDC_2022_Background-375w-812h@3x~iphone.ca   Head Mask, Head, Leg
    7400.WWDC_2022_Floating-375w-812h@3x~iphone.ca     Arm
    7400.WWDC_2022_Foreground-375w-812h@3x~iphone.ca   Body
    Wallpaper.plist
```

Each `.ca` holds `main.caml` (XML), `index.xml` (document geometry, 375x812),
`assetManifest.caml` (an empty `MicaAssetManifest` module list) and `assets/`.

`main.caml` references images two ways, and both occur:

```xml
<contents type="CGImage" src="assets/Head.HEIC"/>
<contents type="CGImage" src="assets/Head%20Mask.HEIC"/>
```

Layer `name=` attributes also carry `.HEIC` (`name="Leg.HEIC"`). Those are
labels, not paths: the animation's `LKStateSetValue targetId=` refers to layer
`id=`, so they are left alone.

Scaling: the source images are 1105x1160 to 1326x1326, while the 375x812@3x
screen is 1170 pixels on the long edge, and each layer stretches its contents to
its own `bounds`. `sips -Z 1170` therefore loses nothing visible. The iPad
variant's 2732 is a different screen; copying that number wastes payload for no
gain.

`scripts/make-wallpaper-overlay-alioth.py` performs the replacement in place and
`scripts/build-alioth-ios-root-macos.sh` calls it in the post stage, between
`alioth-ios-root-post.py` and `verify-alioth-ios-root.py`. Verified against a
copy of the mounted 20H392 root: five images converted, all `src=` rewritten,
`index.xml` and `assetManifest.caml` untouched.

If the lock screen is still black with the PNGs in place, the next suspect is
PosterBoard's poster store, not ImageIO: the stored poster can still point at
the sealed HEIC path. The `native-text-fixup` mechanism (`text-fixup0..7` boot
arguments) is how that gets corrected — it rewrites bytes in a signed file only
after the page passed signature validation and only when the original bytes
match, so a different build is left alone. The offsets to relocate are those of
`PRSPosterPathURLResourceValues` / `PRSPosterPathFileAttributes` in 20H392's
`dyld_shared_cache_arm64.13`; they come from 21H461 in the upstream reference and
do not carry over.
