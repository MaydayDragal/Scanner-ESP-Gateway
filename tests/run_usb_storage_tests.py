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
    "esp_check.h", "esp_attr.h", "esp_idf_version.h", "esp_vfs_fat.h", "esp_partition.h",
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
    patched_core = temp / "msc_device.c"
    subprocess.run([sys.executable, str(ROOT / "components/esp_tinyusb/patch_msc_core.py"),
                    str(CORE / "src/class/msc/msc_device.c"), str(patched_core)], check=True)
    subprocess.run([
        sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-include", str(ROOT / "tests/msc_test_config.h"),
        "-I", str(temp), "-I", str(ROOT / "components/esp_tinyusb/include"),
        "-I", str(ROOT / "components/esp_tinyusb/include_private"),
        "-I", str(CORE / "src"), "-I", str(ROOT / "tests"), "-I", str(ROOT / "main"),
        str(ROOT / "tests/test_usb_storage.c"), str(ROOT / "tests/usb_storage_controller_test.c"),
        str(ROOT / "main/storage_handoff_model.c"),
        str(ROOT / "main/storage_mode_model.c"),
        str(ROOT / "components/esp_tinyusb/tinyusb_msc.c"),
        str(ROOT / "components/esp_tinyusb/storage_sdmmc.c"),
        str(patched_core), "-o", str(binary),
    ], check=True)
    failed = []
    cases_run = 0
    for case in ("automatic_ro", "recovery_mount", "mount_setter_failure", "unmount_setter_failure", "unmount_ff_failure", "unmount_persistent", "missing_card", "maintenance", "sync_success", "sync_failure", "eject_success", "eject_prevented", "eject_not_eject", "eject_failed_csw", "eject_failed_cbw", "eject_aborted_cbw", "eject_failed_transfer", "eject_aborted_transfer", "eject_short_transfer", "eject_reset", "eject_reset_during_resume", "eject_disconnect", "eject_reconnect", "eject_bot_reset", "eject_repeated", "read_failure", "write_invalid", "write_callback", "write_failed_data", "event_filter", "write_fifo_detach", "write_active_detach", "write_single", "write_multiple", "write_failure", "quiescence", "timeout", "late_quiescence", "late_ack", "enumerated", "enumeration_timeout", "late_host"):
        cases_run += 1
        result = subprocess.run([str(binary), case])
        if result.returncode:
            failed.append(case)
    print(f"USB lifecycle: {cases_run - len(failed)}/{cases_run} passed")
    if failed:
        raise SystemExit("Failed lifecycle cases: " + ", ".join(failed))
