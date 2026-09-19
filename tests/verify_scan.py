"""Decode an actual completed gateway JPEG; requires Pillow."""
import argparse
from pathlib import Path

from PIL import Image

EPSON_QUALITY_50 = {
    0: [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99],
    1: [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99],
}

parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
parser.add_argument("--dpi", type=int, choices=(300, 600), default=300)
parser.add_argument("--quality", type=int, choices=range(1, 101), default=75)
parser.add_argument("--height", type=int, help="expected saved image height in pixels")
args = parser.parse_args()
with Image.open(args.image) as image:
    assert image.format == "JPEG", "Not a JPEG"
    assert image.mode == "RGB", "Not 24-bit color"
    assert image.info.get("dpi") == (args.dpi, args.dpi), "Unexpected DPI metadata"
    assert 0 < image.width <= int(8.5 * args.dpi), "Unexpected saved page width"
    assert 0 < image.height <= 14 * args.dpi and image.height % 8 == 0, "Unexpected acquisition height"
    if args.height is not None:
        assert image.height == args.height, "Unexpected saved page height"
    image.load()  # Reject truncated files; do not enable LOAD_TRUNCATED_IMAGES.
    values = [value for table in image.quantization.values() for value in table]
    if args.quality == 100:
        assert all(value == 1 for value in values), "Unexpected quality 100 tables"
    elif args.quality == 50:
        assert image.quantization == EPSON_QUALITY_50, "Unexpected Epson quality 50 tables"
    else:
        assert any(value > 1 for value in values), "Image still has quality 100 tables"
    print(f"Full JPEG decode passed: {image.width} x {image.height}, RGB, {args.dpi} dpi, quality {args.quality} tables")
    print(f"File size: {args.image.stat().st_size:,} bytes")
