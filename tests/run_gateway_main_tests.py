"""Actual main integration with hardware/network/storage boundary stubs."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = ["retained_failure", "cleanup_warning", "crop_warning", "success_sizes", "supersede",
         "stopped_ack", "clock_invalid", "clock_valid_retry", "idle", "off_exhausted",
         "physical_wake", "status_hold", "sticky_io", "wake_failed", "wake_retry", "retained_wake",
         "acquire_busy", "finalizing_busy", "restore_busy", "alert_maintenance", "render_repeat_lcd", "render_repeat_led"]
with tempfile.TemporaryDirectory(prefix="gateway-main-") as directory:
    work = Path(directory)
    for name in ("esp_err.h", "esp_check.h", "esp_log.h", "esp_timer.h", "esp_netif.h",
                 "esp_event.h", "freertos/FreeRTOS.h", "freertos/task.h"):
        header = work / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "r3_boundary_sdk.h"\n')
    binary = work / "gateway.exe"
    sources = ["tests/test_gateway_main.c", "main/scanner_button_model.c", "main/scanner_idle_model.c",
               "main/scanner_display_model.c", "main/scanner_led_model.c", "main/gateway_state_model.c",
               "main/gateway_diagnostics.c", "main/scanner_clock_model.c"]
    subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-variable", "-I", str(work), "-I", str(ROOT / "tests"), "-I", str(ROOT / "main"),
                    *[str(ROOT / source) for source in sources], "-o", str(binary)], check=True)
    failed = []
    for case in sys.argv[1:] or CASES:
        result = subprocess.run([str(binary), case])
        if result.returncode:
            failed.append(case)
    if failed:
        raise SystemExit("Failed actual gateway main: " + ", ".join(failed))
