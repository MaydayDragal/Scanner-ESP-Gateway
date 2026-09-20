"""Check complete and page-cropped dimensions using decodable JPEGs."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

VERIFIER = Path(__file__).with_name("verify_scan.py")

def verify(path, expected_quality=None, python_optimized=False, dpi=300):
    command = [sys.executable]
    if python_optimized:
        command.append("-O")
    command.extend((str(VERIFIER), str(path), "--dpi", str(dpi)))
    if expected_quality is not None:
        command.extend(("--quality", str(expected_quality)))
    return subprocess.run(command, capture_output=True, text=True)

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
                        result = verify(path, dpi=dpi)
                        self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)
                        if not valid:
                            self.assertIn("Unexpected acquisition height", result.stderr)

    def test_quality_50_image_is_not_accepted_as_quality_75(self):
        with tempfile.TemporaryDirectory(prefix="scan-quality-") as directory:
            path = Path(directory) / "synthetic-quality-50.jpg"
            Image.new("RGB", (256, 128), "white").save(path, quality=50, dpi=(300, 300))
            result = verify(path, expected_quality=75)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("quality unverified", result.stderr)

    def test_registered_quality_50_tables_are_accepted(self):
        with tempfile.TemporaryDirectory(prefix="scan-quality-") as directory:
            path = Path(directory) / "synthetic-quality-50.jpg"
            Image.new("RGB", (256, 128), "white").save(path, quality=50, dpi=(300, 300))
            result = verify(path, expected_quality=50)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("quality 50 tables verified", result.stdout)

    def test_registered_quality_50_rejects_different_tables(self):
        with tempfile.TemporaryDirectory(prefix="scan-quality-") as directory:
            path = Path(directory) / "synthetic-quality-75.jpg"
            Image.new("RGB", (256, 128), "white").save(path, quality=75, dpi=(300, 300))
            result = verify(path, expected_quality=50)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Unexpected Epson quality 50 tables", result.stderr)

    def test_invalid_dimensions_fail_with_optimized_python(self):
        with tempfile.TemporaryDirectory(prefix="scan-dimensions-") as directory:
            path = Path(directory) / "bad-dimensions.jpg"
            Image.new("RGB", (256, 127), "white").save(path, quality=50, dpi=(300, 300))
            result = verify(path, python_optimized=True)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Unexpected acquisition height", result.stderr)

    def test_omitted_quality_reports_unverified_after_decode(self):
        with tempfile.TemporaryDirectory(prefix="scan-quality-") as directory:
            path = Path(directory) / "synthetic-quality-50.jpg"
            Image.new("RGB", (256, 128), "white").save(path, quality=50, dpi=(300, 300))
            result = verify(path)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("quality unverified", result.stdout)

if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
