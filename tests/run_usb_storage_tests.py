"""Run the production controller against deterministic SDK boundary faults."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "tests/vendor/tinyusb"
MANAGED_CORE = ROOT / "managed_components/espressif__tinyusb"
for name, expected in json.loads((CORE / "UPSTREAM_SHA256.json").read_text()).items():
    for base in (CORE, MANAGED_CORE) if MANAGED_CORE.exists() else (CORE,):
        source = base / name
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Pinned TinyUSB 0.21.0~2 source mismatch: {source}")

HEADERS = (
    "esp_err.h", "esp_log.h", "driver/sdmmc_host.h", "sdmmc_cmd.h",
    "diskio_impl.h", "diskio_sdmmc.h", "freertos/FreeRTOS.h",
    "freertos/semphr.h", "freertos/task.h", "tinyusb.h",
    "tinyusb_default_config.h", "sys/stat.h",
    "esp_check.h", "esp_idf_version.h", "esp_vfs_fat.h", "esp_partition.h",
    "esp_memory_utils.h", "soc/soc_caps.h", "sdkconfig.h", "vfs_fat_internal.h", "wear_levelling.h",
)
with tempfile.TemporaryDirectory(prefix="usb-storage-test-") as directory:
    temp = Path(directory)
    for name in HEADERS:
        header = temp / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "usb_storage_test_stubs.h"\n')
    (temp / "tusb_config.h").write_text((ROOT / "tests/msc_test_config.h").read_text())
    binary = temp / "usb_storage_test.exe"
    subprocess.run([
        sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-include", str(ROOT / "tests/msc_test_config.h"),
        "-I", str(temp), "-I", str(ROOT / "components/esp_tinyusb/include"),
        "-I", str(ROOT / "components/esp_tinyusb/include_private"),
        "-I", str(CORE / "src"), "-I", str(ROOT / "tests"), "-I", str(ROOT / "main"),
        str(ROOT / "tests/test_usb_storage.c"), str(ROOT / "tests/usb_storage_controller_test.c"),
        str(ROOT / "main/storage_handoff_model.c"),
        str(ROOT / "components/esp_tinyusb/tinyusb_msc.c"),
        str(ROOT / "components/esp_tinyusb/storage_sdmmc.c"),
        str(CORE / "src/class/msc/msc_device.c"), "-o", str(binary),
    ], check=True)
    failed = []
    for case in ("read_failure", "write_invalid", "write_callback", "event_filter", "write_fifo_detach", "write_active_detach", "write_single", "write_multiple", "write_failure", "quiescence", "timeout", "late_quiescence", "late_ack", "enumerated", "enumeration_timeout", "late_host"):
        result = subprocess.run([str(binary), case])
        if result.returncode:
            failed.append(case)
    if failed:
        raise SystemExit("Failed lifecycle cases: " + ", ".join(failed))
