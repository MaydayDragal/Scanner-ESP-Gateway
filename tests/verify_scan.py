"""Decode an actual completed gateway JPEG; requires Pillow."""
import argparse
from pathlib import Path

from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
args = parser.parse_args()
with Image.open(args.image) as image:
    assert image.format == "JPEG", "Not a JPEG"
    assert image.mode == "RGB", "Not 24-bit color"
    assert image.info.get("dpi") == (600, 600), "Unexpected DPI metadata"
    assert image.width == 5100, "Unexpected acquisition width"
    image.load()  # Reject truncated files; do not enable LOAD_TRUNCATED_IMAGES.
    assert all(value == 1 for table in image.quantization.values() for value in table), "Unexpected quality tables"
    print(f"Full JPEG decode passed: {image.width} x {image.height}, RGB, 600 dpi, quality 100 tables")
    print(f"File size: {args.image.stat().st_size:,} bytes")
