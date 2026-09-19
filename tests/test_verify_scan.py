"""Check complete and page-cropped dimensions using decodable JPEGs."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

VERIFIER = Path(__file__).with_name("verify_scan.py")

class ScanDimensionsTest(unittest.TestCase):
    def test_page_height_is_within_the_acquisition_canvas(self):
        with tempfile.TemporaryDirectory(prefix="scan-dimensions-") as directory:
            path = Path(directory) / "scan.jpg"
            for dpi in (300, 600):
                for height, valid in ((14 * dpi, True), (14 * dpi - 8, True),
                                      (14 * dpi - 1, False), (14 * dpi + 8, False)):
                    with self.subTest(dpi=dpi, height=height):
                        image = Image.new("RGB", (int(8.5 * dpi), height), "white")
                        image.save(path, quality=100, dpi=(dpi, dpi))
                        image.close()
                        result = subprocess.run(
                            [sys.executable, str(VERIFIER), str(path), "--dpi", str(dpi), "--quality", "100"],
                            capture_output=True, text=True,
                        )
                        self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)
                        if not valid:
                            self.assertIn("Unexpected acquisition height", result.stderr)

if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
