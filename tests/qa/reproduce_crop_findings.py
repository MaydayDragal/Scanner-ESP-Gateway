"""Isolated reproductions against unchanged gateway crop sources. No device access.
Run: python reproduce_crop_findings.py [repository_root]
Requires Pillow and ziglang. Each run creates a fresh artifact directory in TEMP.
These assertions reproduce known defects in the reviewed version, not correct
behavior. A failing assertion after a fix means this diagnostic needs updating.
"""
import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image, ImageChops

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix='scanner-qa-crop-'))
print('Artifacts:', work, flush=True)
library = work / 'crop.dll'
subprocess.run([
    sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-shared', '-I', str(root / 'main'), str(root / 'main/jpeg_crop.c'),
    str(root / 'main/jpeg_width_crop.c'), '-o', str(library),
], check=True)
dll = ctypes.CDLL(str(library))
height_crop = dll.jpeg_crop_file
height_crop.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_uint16]
height_crop.restype = ctypes.c_bool
width_crop = dll.jpeg_width_crop_file
width_crop.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint16)]
width_crop.restype = ctypes.c_int


def scanner_bytes(image):
    base = work / 'encoder-output.jpg'
    image.save(base, quality=75, subsampling=1, restart_marker_rows=1, dpi=(300, 300))
    data = bytearray(base.read_bytes())
    sof = data.index(b'\xff\xc0')
    data[sof + 5:sof + 7] = (4200).to_bytes(2, 'big')
    return data


def run(name, data, preexisting_target=False):
    source = work / (name + '.TMP')
    target = work / (name + '.CRP')
    source.write_bytes(data)
    if preexisting_target:
        target.write_bytes(b'PREEXISTING USER DATA')
    height_ok = height_crop(str(source).encode(), 2550, 384)
    width = ctypes.c_uint16()
    result = width_crop(str(source).encode(), str(target).encode(), ctypes.byref(width))
    print(name, 'height_ok=', height_ok, 'width_result=', result,
          'width=', width.value, 'target_exists=', target.exists(), flush=True)
    assert height_ok
    return source, target, result, width.value


# A full-width page with legitimate dark printed/photo content. The small white
# item is in row 1; width detection samples only even rows at this height.
page = Image.new('RGB', (2550, 384), 'white')
page.paste((8, 8, 8), (1536, 0, 2550, 384))
page.paste('white', (2000, 8, 2400, 16))
source, target, result, width = run('content-loss', scanner_bytes(page))
assert result == 1 and width == 1552
with Image.open(target) as after:
    after.load()
    assert after.size == (1552, 384)
print('CONFIRMED: item x=2000..2399,y=8..15 is removed; capture deletes original at scanner_capture.c:127.')

# Delete every entropy byte from MCU row 1, preserving restart markers.
page = Image.new('RGB', (2550, 384), 'white')
data = scanner_bytes(page)
start = data.index(b'\xff\xd0') + 2
end = data.index(b'\xff\xd1', start)
del data[start:end]
source, target, result, width = run('invalid-unsampled-row', data)
assert result == 0 and width == 2550
with Image.open(source) as decoded:
    decoded.load()
    bounds = ImageChops.difference(page, decoded).getbbox()
    assert bounds == (0, 8, 2550, 16), bounds
print('CONFIRMED: invalid empty row passes both validators; decoder reconstructs damaged band', bounds)

# Ordinary no-crop success silently deletes a target it did not create.
source, target, result, width = run('preexisting-target', scanner_bytes(page), True)
assert result == 0 and not target.exists()
print('CONFIRMED: valid no-crop scan deletes preexisting .CRP target at jpeg_width_crop.c:338.')

# Current deliberate half-canvas safety guard prevents receipt-sized cropping.
page = Image.new('RGB', (2550, 384), (8, 8, 8))
page.paste('white', (0, 0, 600, 384))
source, target, result, width = run('narrow-receipt', scanner_bytes(page))
assert result == 0 and width == 2550
print('LIMITATION: 600px receipt retains entire 2550px canvas.')
print('All targeted reproductions confirmed. Working sources unchanged.')
