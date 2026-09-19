"""Run the production controller against deterministic SDK boundary faults."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HEADERS = (
    "esp_err.h", "esp_log.h", "driver/sdmmc_host.h", "sdmmc_cmd.h",
    "diskio_impl.h", "diskio_sdmmc.h", "freertos/FreeRTOS.h",
    "freertos/semphr.h", "freertos/task.h", "tinyusb.h",
    "tinyusb_default_config.h", "tinyusb_msc.h", "device/usbd_pvt.h", "sys/stat.h",
)
with tempfile.TemporaryDirectory(prefix="usb-storage-test-") as directory:
    temp = Path(directory)
    for name in HEADERS:
        header = temp / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "usb_storage_test_stubs.h"\n')
    binary = temp / "usb_storage_test.exe"
    subprocess.run([
        sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(temp), "-I", str(ROOT / "tests"), "-I", str(ROOT / "main"),
        str(ROOT / "tests/test_usb_storage.c"), str(ROOT / "main/usb_storage.c"),
        str(ROOT / "main/storage_handoff_model.c"), "-o", str(binary),
    ], check=True)
    failed = []
    for case in ("quiescence", "timeout", "late_quiescence", "late_ack", "enumerated", "enumeration_timeout", "late_host"):
        result = subprocess.run([str(binary), case])
        if result.returncode:
            failed.append(case)
    if failed:
        raise SystemExit("Failed lifecycle cases: " + ", ".join(failed))
