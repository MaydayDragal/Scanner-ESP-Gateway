"""Focused R3 model, actual button driver, and actual main integration tests."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="storage-mode-test-") as directory:
    temp = Path(directory)
    for name in ("esp_err.h", "esp_check.h", "esp_log.h", "esp_timer.h", "esp_netif.h",
                 "esp_event.h", "freertos/FreeRTOS.h", "freertos/task.h", "freertos/queue.h", "driver/gpio.h"):
        header = temp / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "r3_boundary_sdk.h"\n')
    binary = temp / "r3.exe"
    base = [sys.executable, "-m", "ziglang", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-variable", "-I", str(temp), "-I", str(ROOT / "tests"), "-I", str(ROOT / "main")]
    groups = [
        (["tests/test_storage_mode_model.c", "main/storage_mode_model.c"], [None]),
        (["tests/test_scanner_button_model.c", "main/scanner_button_model.c"], [None]),
        (["tests/test_scanner_button_driver.c", "main/scanner_button.c", "main/scanner_button_model.c"],
         ["normal", "gpio_error", "queue_error", "task_error"]),
        (["tests/test_storage_main.c", "main/scanner_button_model.c", "main/scanner_idle_model.c", "main/scanner_display_model.c",
          "main/gateway_state_model.c", "main/gateway_diagnostics.c", "main/scanner_clock_model.c"],
         ["maintenance_paper", "missing_card", "recovery_mount", "awake_hold", "wake_only", "before_eject", "after_eject", "resume_delayed", "resume_render_delayed", "enter_delayed", "recovery_delayed", "sleep_delayed"]),
    ]
    failed = []
    for sources, cases in groups:
        subprocess.run(base + [str(ROOT / source) for source in sources] + ["-o", str(binary)], check=True)
        for case in cases:
            result = subprocess.run([str(binary)] + ([case] if case else []))
            if result.returncode:
                failed.append(case or sources[0])
    if failed:
        raise SystemExit("Failed R3 cases: " + ", ".join(failed))
