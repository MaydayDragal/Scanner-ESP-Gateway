# Scanner Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show live ES-60W connection, feeder, battery, scan progress, and last-capture information on the onboard 1.47-inch display.

**Architecture:** A pure C display model formats complete snapshots. A small `esp_lcd` ST7789 renderer draws those snapshots from the main task with a bounded DMA strip buffer. Capture reports bytes at 1 MiB intervals.

**Tech Stack:** ESP-IDF 5.5.5, `esp_lcd`, SPI2, ST7789, portable C host tests

**Spec:** `docs/superpowers/specs/2026-09-18-scanner-display-design.md`

## Global Constraints

- Keep scanning and WebDAV operational if display initialization fails.
- Use 320 x 172 landscape orientation on the Waveshare ESP32-S3-LCD-1.47.
- Do not add LVGL or a full-screen framebuffer.
- Do not display battery percentage or scan percentage because the scanner does not expose them.
- Preserve the configured 300 dpi RGB JPEG capture behavior; quality was later changed from 100 to 50.

---

### Task 1: Preserve Scanner Battery Status

**Files:**
- Modify: `main/esci_scan.h`
- Modify: `main/esci_scan.c`
- Modify: `main/scanner_capture.h`
- Modify: `main/scanner_capture.c`
- Modify: `tests/test_esci_scan.c`

**Interfaces:**
- Produces: `esci_status_t esci_scanner_status(const esci_io_t *io)` and `esci_status_t scanner_status(uint32_t gateway_ip)`.

- [x] Add failing assertions for loaded/empty paper with low-battery preservation.
- [x] Run `tests/run_protocol_tests.ps1` and confirm compilation or assertions fail for the missing status API.
- [x] Add `esci_status_t`, parse `#BATLOW ` into it, and expose it through scanner capture.
- [x] Run `tests/run_protocol_tests.ps1` and confirm all protocol tests pass.

### Task 2: Add the Portable Display Model

**Files:**
- Create: `main/scanner_display_model.h`
- Create: `main/scanner_display_model.c`
- Create: `tests/test_scanner_display_model.c`
- Modify: `tests/run_protocol_tests.ps1`

**Interfaces:**
- Produces: `scanner_display_state_t`, `scanner_display_view_t`, and `scanner_display_format(const scanner_display_state_t *, scanner_display_view_t *)`.

- [x] Add failing tests for boot, ready, low battery, scanning bytes, completed scan, and error formatting.
- [x] Run `tests/run_protocol_tests.ps1` and confirm the display-model test fails to compile.
- [x] Implement bounded formatting with fixed-size strings and status colors.
- [x] Run `tests/run_protocol_tests.ps1` and confirm all host tests pass.

### Task 3: Add the ST7789 Renderer

**Files:**
- Create: `main/scanner_display.h`
- Create: `main/scanner_display.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `scanner_display_state_t` and `scanner_display_format`.
- Produces: `bool scanner_display_start(void)` and `void scanner_display_show(const scanner_display_state_t *state)`.

- [x] Implement SPI2/ST7789 initialization for the documented board pins, landscape rotation, BGR color order, inversion, panel gap, and active-high backlight.
- [x] Implement 5 x 7 text, rectangles, status colors, and a 320 x 16 DMA strip renderer that waits for each transfer before buffer reuse.
- [x] Make initialization and render failures disable display updates without aborting the gateway.
- [x] Run `eim run 'idf.py build' v5.5.5` and confirm the firmware builds.

### Task 4: Integrate Live State and Progress

**Files:**
- Modify: `main/scanner_capture.h`
- Modify: `main/scanner_capture.c`
- Modify: `main/main.c`
- Modify: `README.md`

**Interfaces:**
- Produces: `scanner_progress_fn` callback supplied to `scanner_capture` and called after each 1 MiB interval.
- Consumes: display APIs from Task 3 and scanner status from Task 1.

- [x] Add the progress callback to capture context and invoke it after each successfully written MiB.
- [x] Initialize the display before SD and network startup and publish boot, connection, feeder, scanning, completion, and failure snapshots.
- [x] Preserve last successful scan details when a later scan fails.
- [x] Document the screen contents and headless fallback.
- [x] Run host tests, `git diff --check`, and a clean ESP-IDF build.

### Task 5: Flash and Hardware Verify

**Files:**
- Modify: `docs/scanner-protocol-notes.md`
- Modify: this plan checklist

**Interfaces:**
- Consumes: the completed firmware binary and existing BOOT/RESET flashing process.

- [ ] Flash through the download-mode COM port and restart the application.
- [ ] Verify upright landscape output, readable colors, Wi-Fi and feeder transitions, low-battery indication when exposed, live MiB updates, and final filename/size/duration.
- [ ] Feed a new page, copy a prior image through S: during capture, then decode the new JPEG and compare transferred hashes.
- [ ] Record observed hardware results and any orientation correction.
