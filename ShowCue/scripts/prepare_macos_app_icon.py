#!/usr/bin/env python3
"""Prepare ShowCue macOS app icon (opaque, full-bleed) + native iconutil .icns."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "Resources"
SOURCE = RESOURCES / "AppIconSource.png"
OUTPUT_PNG = RESOURCES / "AppIcon.png"
ABOUT_OUTPUT = RESOURCES / "AppIconAbout.png"
ICONSET_DIR = RESOURCES / "AppIcon.iconset"
OUTPUT_ICNS = RESOURCES / "AppIcon.icns"
CANVAS = 1024
CONTENT = 864  # ~84% — safe zone inside macOS squircle mask


def sample_background_colour(image: Image.Image) -> tuple[int, int, int]:
    rgb = image.convert("RGB")
    pixels = [
        rgb.getpixel((0, 0)),
        rgb.getpixel((rgb.width - 1, 0)),
        rgb.getpixel((0, rgb.height - 1)),
        rgb.getpixel((rgb.width - 1, rgb.height - 1)),
    ]
    return (
        sum(p[0] for p in pixels) // len(pixels),
        sum(p[1] for p in pixels) // len(pixels),
        sum(p[2] for p in pixels) // len(pixels),
    )


def prepare_master_png(source: Path) -> Image.Image:
    art = Image.open(source).convert("RGBA")
    bg = sample_background_colour(art)

    art.thumbnail((CONTENT, CONTENT), Image.Resampling.LANCZOS)

    canvas = Image.new("RGBA", (CANVAS, CANVAS), bg + (255,))
    offset = ((CANVAS - art.width) // 2, (CANVAS - art.height) // 2)

    # Flatten artwork onto opaque background — macOS applies squircle mask itself.
    layer = Image.new("RGBA", (CANVAS, CANVAS), bg + (0,))
    layer.alpha_composite(art, offset)
    flattened = Image.new("RGB", (CANVAS, CANVAS), bg)
    flattened.paste(layer, mask=layer.split()[-1])
    return flattened.convert("RGBA")


def write_iconset(master: Image.Image, iconset_dir: Path) -> None:
    if iconset_dir.exists():
        for child in iconset_dir.iterdir():
            child.unlink()
    else:
        iconset_dir.mkdir(parents=True)

    entries = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }

    for name, size in entries.items():
        resized = master.convert("RGB").resize((size, size), Image.Resampling.LANCZOS)
        resized.save(iconset_dir / name, format="PNG", optimize=True)


def write_icns(iconset_dir: Path, icns_path: Path) -> bool:
    if icns_path.exists():
        icns_path.unlink()

    try:
        subprocess.run(
            ["iconutil", "-c", "icns", str(iconset_dir), "-o", str(icns_path)],
            check=True,
            capture_output=True,
            text=True,
        )
        return True
    except (FileNotFoundError, subprocess.CalledProcessError) as err:
        print(
            f"iconutil skipped ({err}); AppIcon.png still updated for bundle.",
            file=sys.stderr,
        )
        return False


def prepare_icon(source: Path) -> bool:
    if not source.exists():
        raise FileNotFoundError(f"Missing source icon: {source}")

    master = prepare_master_png(source)
    master.save(OUTPUT_PNG, format="PNG", optimize=True)
    master.resize((256, 256), Image.Resampling.LANCZOS).save(
        ABOUT_OUTPUT, format="PNG", optimize=True
    )
    write_iconset(master, ICONSET_DIR)
    return write_icns(ICONSET_DIR, OUTPUT_ICNS)


def main() -> int:
    source = SOURCE
    if len(sys.argv) > 1:
        source = Path(sys.argv[1])

    if not source.exists():
        print("No icon source found.", file=sys.stderr)
        return 1

    wrote_icns = prepare_icon(source)
    print(f"Wrote {OUTPUT_PNG}")
    print(f"Wrote {ABOUT_OUTPUT}")
    if wrote_icns:
        print(f"Wrote {OUTPUT_ICNS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
