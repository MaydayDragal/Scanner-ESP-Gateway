"""Decode a completed gateway JPEG and verify only documented scanner tables."""
import argparse
import json
from pathlib import Path
import sys

from PIL import Image, UnidentifiedImageError


TABLES = Path(__file__).with_name("fixtures") / "epson_quantization.json"


def check_scan(path, dpi, height, quality):
    with Image.open(path) as image:
        if image.format != "JPEG":
            raise ValueError("Not a JPEG")
        if image.mode != "RGB":
            raise ValueError("Not 24-bit color")
        if image.info.get("dpi") != (dpi, dpi):
            raise ValueError("Unexpected DPI metadata")
        if not 0 < image.width <= int(8.5 * dpi):
            raise ValueError("Unexpected saved page width")
        if not (0 < image.height <= 14 * dpi and image.height % 8 == 0):
            raise ValueError("Unexpected acquisition height")
        if height is not None and image.height != height:
            raise ValueError("Unexpected saved page height")
        image.load()  # Reject truncated files; do not enable LOAD_TRUNCATED_IMAGES.

        if quality is not None:
            registry = json.loads(TABLES.read_text(encoding="utf-8"))
            entry = registry.get(str(quality))
            if not entry or entry.get("verified") is not True or not entry.get("tables"):
                raise ValueError(f"quality unverified: no registered Epson quality {quality} tables")
            observed = {str(index): values for index, values in image.quantization.items()}
            if observed != entry["tables"]:
                raise ValueError(f"Unexpected Epson quality {quality} tables")
            quality_status = f"quality {quality} tables verified"
        else:
            quality_status = "quality unverified (no --quality requested)"

        return image.width, image.height, quality_status


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--dpi", type=int, choices=(300, 600), default=300)
    parser.add_argument("--quality", type=int, choices=range(1, 101))
    parser.add_argument("--height", type=int, help="expected saved image height in pixels")
    args = parser.parse_args()
    try:
        width, height, quality_status = check_scan(args.image, args.dpi, args.height, args.quality)
    except (OSError, ValueError, UnidentifiedImageError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"Scan verification failed: {error}", file=sys.stderr)
        return 1
    print(f"Full JPEG decode passed: {width} x {height}, RGB, {args.dpi} dpi; {quality_status}")
    print(f"File size: {args.image.stat().st_size:,} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
