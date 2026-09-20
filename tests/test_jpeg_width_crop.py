"""Exercise lossless right-edge cropping with actual restart-marked JPEGs."""
import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops


root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="jpeg-width-test-") as library_directory:
    library = Path(library_directory) / "jpeg_width_crop_test.dll"
    subprocess.run(
        [sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-shared", "-I", str(root / "main"), str(root / "main" / "jpeg_width_crop.c"),
         str(root / "main" / "jpeg_stream.c"), "-o", str(library)],
        check=True,
    )
    loaded_library = ctypes.CDLL(str(library))
    crop = loaded_library.jpeg_width_crop_file
    crop.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint16)]
    crop.restype = ctypes.c_int


    def run_crop(source: Path, target: Path):
        width = ctypes.c_uint16(0)
        result = crop(str(source).encode(), str(target).encode(), ctypes.byref(width))
        return result, width.value


    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        source, target = work / "source.jpg", work / "cropped.jpg"
        original = Image.new("RGB", (640, 128), "white")
        original.paste((8, 8, 8), (320, 0, 640, 128))
        original.save(source, quality=50, subsampling=1, restart_marker_rows=1)
        raw = source.read_bytes()
        assert b"\xff\xdd" in raw and b"\xff\xd0" in raw
        result, width = run_crop(source, target)
        assert result == 1 and 320 <= width <= 352, (result, width)
        assert source.read_bytes() == raw
        with Image.open(source) as before, Image.open(target) as after:
            before.load()
            after.load()
            assert after.size == (width, 128)
            assert ImageChops.difference(before.crop((0, 0, width, 128)), after).getbbox() is None

        target.unlink()
        Image.new("RGB", (640, 128), "white").save(
            source, quality=50, subsampling=1, restart_marker_rows=1
        )
        result, width = run_crop(source, target)
        assert result == 0 and width == 640 and not target.exists()

        # Mixed widths: a full-width page stays intact; a narrower one trims only
        # the uniformly dark region beyond its right edge.
        wide = Image.new("RGB", (5100, 128), "white")
        wide.save(source, quality=75, subsampling=1, restart_marker_rows=1)
        result, width = run_crop(source, target)
        assert result == 0 and width == 5100 and not target.exists()

        wide.paste((8, 8, 8), (3300, 0, 5100, 128))
        wide.save(source, quality=75, subsampling=1, restart_marker_rows=1)
        result, width = run_crop(source, target)
        assert result == 1 and 3296 <= width <= 3344, (result, width)
        with Image.open(target) as after:
            after.load()
            assert after.size == (width, 128)
        target.unlink()

        source.write_bytes(b"not a JPEG")
        result, _ = run_crop(source, target)
        assert result == -1 and not target.exists()

    if sys.platform == "win32":
        del crop
        free_library = ctypes.WinDLL("kernel32", use_last_error=True).FreeLibrary
        free_library.argtypes = [ctypes.c_void_p]
        free_library.restype = ctypes.c_int
        if not free_library(loaded_library._handle):
            raise OSError(ctypes.get_last_error(), "Could not unload width-test DLL")

    print("JPEG width crop tests passed")
