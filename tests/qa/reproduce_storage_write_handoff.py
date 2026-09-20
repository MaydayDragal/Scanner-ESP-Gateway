"""Read-only host reproduction of two storage write defects. No USB/card access.

Usage: python reproduce_storage_write_handoff.py "C:\\Scanner ESP Gateway"
Requires Python's ziglang package, as do the project's existing storage tests.
Builds and removes temporary C/exe files; does not change repository source.

The controller is compiled directly from the given repository. Four MSC write
functions are extracted from its managed esp_tinyusb dependency at runtime.
The RTOS/SDK harness is a snapshot of the project's existing lifecycle stubs,
extended to use a FIFO event queue and a configurable failing SD medium.
Unreachable SPIFlash overflow builtins are aliased for a 64-bit host compiler.
The simulated queue ordering is possible in production; this is not hardware
reproduction or proof of the cause of any historical FAT corruption.

Both cases pass when the defect is reproduced (asserted bad outcome), so a
future fix should make the corresponding assertion fail until expectations
are changed to express the repaired behavior.
"""
from pathlib import Path
import argparse
import subprocess
import sys
import tempfile

DEPENDENCY_FUNCTIONS = ['msc_storage_write_sector', 'tusb_write_func', 'msc_storage_write_sector_deferred', 'tud_msc_write10_cb']
TEMPLATE = Path(__file__).with_name("storage_write_handoff.c.in").read_text(encoding="utf-8")
HEADERS = ("esp_err.h", "esp_log.h", "driver/sdmmc_host.h", "sdmmc_cmd.h",
           "diskio_impl.h", "diskio_sdmmc.h", "freertos/FreeRTOS.h",
           "freertos/semphr.h", "freertos/task.h", "tinyusb.h",
           "tinyusb_default_config.h", "tinyusb_msc.h", "device/usbd_pvt.h", "sys/stat.h")

def extract_function(source, name):
    position = source.index(name + "(")
    start = source.rfind("\n", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end] + "\n"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path, nargs="?", default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.repository.resolve()
    dependency = (root / "managed_components/espressif__esp_tinyusb/tinyusb_msc.c").read_text()
    source = TEMPLATE
    for name in DEPENDENCY_FUNCTIONS:
        source = source.replace("@@" + name + "@@\n", extract_function(dependency, name))
    with tempfile.TemporaryDirectory(prefix="scanner-storage-qa-") as directory:
        temp = Path(directory)
        for name in HEADERS:
            header = temp / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "usb_storage_test_stubs.h"\n')
        harness = temp / "repro.c"
        harness.write_text(source)
        binary = temp / "repro.exe"
        command = [sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(temp), "-I", str(root / "tests"), "-I", str(root / "main"),
                   str(harness), str(root / "main/usb_storage.c"),
                   str(root / "main/storage_handoff_model.c"), "-o", str(binary)]
        subprocess.run(command, check=True)
        for case in ("drop_on_detach", "sd_write_failure"):
            print(case, flush=True)
            subprocess.run([str(binary), case], check=True)

if __name__ == "__main__":
    main()
