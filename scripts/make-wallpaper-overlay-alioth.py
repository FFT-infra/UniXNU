#!/usr/bin/env python3
"""Replace the default alioth wallpaper's HEIC layers with PNG, in place.

  make-wallpaper-overlay-alioth.py MOUNTED_ROOT

MOUNTED_ROOT is the mounted iOS root volume that build-alioth-ios-root-macos.sh
assembled. The iPhone X (iPhone10,3) default on 20H392 is wallpaper 7400, the
iOS 16 WWDC layered animation, under

  Library/Wallpaper/Collections/iOS_16~iphone.wallpaperCollection/Wallpapers/
    7400.WWDC_2022-375w-812h@3x~iphone.wallpaper/

and it is three .ca documents: Background, Floating, Foreground. Between them
they reference five HEIC images (Head Mask, Head, Leg, Arm, Body). ImageIO
cannot decode HEIC without the hardware HEVC decoder, which this kernel does not
bring up, so PosterBoard renders nothing and the lock screen stays black.

Each image becomes a PNG next to the .ca, named as the HEIC was, and every
`src="assets/<name>.HEIC"` in main.caml becomes `src="<name>.png"`. The layer
`name=` attributes keep their .HEIC text: they are labels, not paths, and the
animation's states address layers by id, not by name.

Scaling: the images are 1.5x to 1.8x the 1170-pixel height the 375x812@3x screen
needs, and the layers stretch their contents to their bounds, so `sips -Z 1170`
on the long edge loses nothing visible. The iPad variant's 2732 is not this
screen; do not copy it.

index.xml and assetManifest.caml are left alone: the first holds the document
geometry (375x812) and the second is an empty module list.

The change is written in place so that the rest of the root image, and the
PosterBoard poster store that refers to these paths, stay valid.
"""
import subprocess
import sys
from pathlib import Path

COLLECTION = (
    "Library/Wallpaper/Collections/iOS_16~iphone.wallpaperCollection/Wallpapers/"
    "7400.WWDC_2022-375w-812h@3x~iphone.wallpaper"
)
EDGE = 1170  # 375x812 at 3x is 1170 pixels on the long edge


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    collection = Path(sys.argv[1]) / COLLECTION
    documents = sorted(collection.glob("*.ca"))
    if not documents:
        sys.exit("No layered wallpaper at " + str(collection))

    converted = 0
    for document in documents:
        main_caml = document / "main.caml"
        if not main_caml.is_file():
            sys.exit("No main.caml in " + str(document))
        text = main_caml.read_text()
        for heic in sorted((document / "assets").glob("*.HEIC")):
            png = document / (heic.stem + ".png")
            subprocess.run(
                ["sips", "-Z", str(EDGE), "-s", "format", "png", str(heic), "--out", str(png)],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            text = text.replace(f'src="assets/{heic.name}"', f'src="{png.name}"')
            text = text.replace(
                f'src="assets/{heic.name.replace(" ", "%20")}"', f'src="{png.name}"'
            )
            heic.unlink()
            converted += 1
        if 'src="assets/' in text:
            sys.exit("main.caml still points at an asset in " + str(document))
        main_caml.write_text(text)
        print(f"{document.name}: {len(list(document.glob('*.png')))} png")

    print(f"{converted} images converted to png at {EDGE}px")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
