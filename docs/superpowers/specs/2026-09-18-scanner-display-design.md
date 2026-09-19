# Scanner Status Display Design

## Goal

Use the Waveshare ESP32-S3-LCD-1.47 onboard display to show the state of the ES-60W scanner and the most recent capture without disrupting scanning, SD writes, or USB WebDAV service.

## Hardware

The board has a 172 x 320 ST7789 panel. Firmware will use it in 320 x 172 landscape orientation. The LCD uses SPI2 with MOSI GPIO45, clock GPIO40, chip select GPIO42, data/command GPIO41, reset GPIO39, and active-high backlight GPIO48. These pins do not overlap the SDMMC pins already used by the project.

## Screen

The screen contains a title, connection state, feeder state, low-battery warning, current action, live received-byte count, last saved filename/size/duration, and the fixed `300 DPI | RGB | JPG 100` settings. Status colors are green for ready or complete, amber for waiting or warning, blue for active scanning, and red for errors.

The display shows byte progress without claiming a percentage because ESC/I-2 does not announce the final JPEG size before capture. Low battery is a boolean warning because the scanner does not expose a percentage.

## Architecture

`scanner_display_model` owns platform-independent state and text formatting. `scanner_display` owns ST7789 initialization and raster output. `main.c` translates Wi-Fi, feeder, and capture events into complete display states. `scanner_capture` reports byte progress at 1 MiB intervals.

Rendering uses a built-in 5 x 7 bitmap font and a reusable 320 x 16 RGB565 DMA strip buffer. Each strip is completed before reuse. No LVGL dependency or full-screen framebuffer is added. Display initialization or drawing errors are logged and disable later screen updates while the scanner gateway continues headless.

## Alternatives

- LVGL provides richer widgets but adds a component, task, timers, and larger buffers for a static status screen.
- A custom raw SPI driver saves little because ESP-IDF already provides the ST7789 command and DMA path.
- The selected `esp_lcd` plus small renderer approach uses existing framework code and has predictable memory use.

## Update Rules

- Boot: starting gateway.
- Wi-Fi disconnected: reconnecting, feeder unavailable.
- Connected with empty feeder: ready, insert paper.
- Loaded/debouncing: paper detected.
- Capture: scanning with bytes received, updated once per MiB.
- Success: complete with filename, bytes, and duration.
- Failure: show the capture error and retain the last successful scan details.
- Battery warning: show `BATTERY LOW` until a later status response clears it.

## Validation

Host tests cover scanner-status parsing and display-model formatting. The ESP-IDF build validates driver integration. Hardware validation checks orientation and colors, then observes boot, waiting, scanning progress, completion, error-safe operation, and a new scan while WebDAV remains available.
