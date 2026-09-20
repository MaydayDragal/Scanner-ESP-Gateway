# Scanner ESP Gateway

Firmware for a Waveshare ESP32-S3 1.47-inch display board that collects scans from an Epson WorkForce ES-60W, saves them to microSD, and presents the card to Windows as a writable USB mass-storage drive while idle.

## Active connection

```text
ES-60W -- Wi-Fi Direct --> ESP32-S3 -- microSD --> USB MSC --> Windows
```

The board has one USB data connection. Scanner control uses the ES-60W's verified ESC/I-2 protocol over Wi-Fi Direct, leaving USB available for mass storage.

## Automatic scan cycle and storage ownership

The microSD FAT volume has one owner at a time. While idle, Windows owns it through writable USB MSC and can add, edit, rename, or delete files. When the feeder detects a new page, the firmware disconnects the USB drive, mounts the card only on the ESP, captures and closes the scan, unmounts the card, and reconnects the drive to Windows.

The drive disappears during every scan and reappears afterward. Finish Windows file operations and close open files before inserting a page; a write in progress is interrupted when the drive disconnects and may leave an incomplete file or filesystem changes. The firmware never formats the card. Files already on the card remain unless Windows deletes or changes them.

After reconnecting, firmware waits up to five seconds for USB host configuration before completing the handoff. If no host is detected, it completes local USB ownership and keeps MSC ready for a later connection; the log reports that the host is unconfigured. Host configuration does not prove Windows has finished mounting the volume.

Successful scans use the next available `SCANnnnn.JPG` name. An interrupted capture remains as a `.TMP` file and does not count as a completed scan. Insert successive pages without resetting the ESP. After a failed scan, remove the page briefly before reloading it. Keep the scanner charging because low-battery status is recognized alongside paper status.

## Scan settings and interface

Current settings are **300 dpi RGB, scanner JPEG quality 75**, using a 2550 x 4200 acquisition canvas. The scanner's original JPEG bytes are received and saved in **16 KiB chunks** without another encode. Before publishing each JPG, the ESP crops the page height to the encoded row boundary and removes a broad dark area at the right of narrow pages. The right-edge crop copies retained compressed JPEG data without recompressing it. If validation fails, the scan stays as a `.TMP` file for inspection.

The onboard 320 x 172 landscape display shows scanner connection, feeder and low-battery status, the current action, received MiB during capture, the last saved filename, size, duration, and the active scan settings. Display initialization or transfer failure is logged and scanning continues headless.

After five minutes without a page or scanner-status change, the display and status LED turn off. The gateway keeps checking the feeder and wakes both indicators when a page is inserted or scanner status changes.

The onboard RGB status LED mirrors the same state: white while starting, yellow while reconnecting or unavailable, green while ready, cyan when paper is detected, blue while scanning, bright green after completion, red after failure, and orange for a low-battery warning. This board uses RGB wire order; the green ready state was verified on the device. LED failure does not stop the gateway.

## Verification

- Install the pinned host test packages: `python -m pip install -r tests/requirements.txt` (Pillow 12.3.0 and ziglang 0.16.0).
- Host protocol, storage lifecycle, image-verifier, and JPEG crop tests: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1` (or add `-Python <python.exe>`). The runner works in a clean checkout with no `build` directory.
- USB MSC hardware check: `powershell -ExecutionPolicy Bypass -File tests/usb_msc_smoke.ps1`.
- Image validation: `python tests/verify_scan.py image.JPG` checks full decode, RGB, 300 dpi metadata, and page dimensions. It reports **quality unverified** unless `--quality` is supplied. Explicit quality verification succeeds only for a registered Epson quantization-table set. The recorded quality-50 tables are in `tests/fixtures/epson_quantization.json`; a historical scanner observation is documented in [protocol notes](docs/scanner-protocol-notes.md). There is no retained table set for quality 75, so `--quality 75` exits nonzero with `quality unverified` until a known scanner sample supplies it. Synthetic test JPEGs are not evidence of scanner output.
- CI runs the host suite and a clean ESP-IDF 5.5.5 build with job-local dummy Wi-Fi values. It does not publish firmware binaries.

Hardware verification on 2026-09-18 passed: Windows enumerated the device as a writable local FAT32 USB disk at `S:`. File creation, readback, rename, and deletion passed. A host-written file survived a full scan handoff unchanged. The previous 600 dpi quality-50 setting produced fully decoded narrow and full-width scans with automatic width selection. See [protocol notes](docs/scanner-protocol-notes.md) for filenames and timings.

## Build and flash

Copy `main/scanner_wifi_local.h.example` to ignored `main/scanner_wifi_local.h` and enter the SSID and password printed on the scanner label. Firmware binaries contain these credentials and must remain private.

To timestamp new scans, add `TIME_WIFI_SSID` and `TIME_WIFI_PASSWORD` for a 2.4 GHz home network to that ignored header. At startup the ESP briefly connects to home Wi-Fi, synchronizes Internet time, then reconnects to the scanner. FAT file dates use Eastern time with daylight saving. Existing files keep their original timestamps.

Build using ESP-IDF v5.5.5, with `esp_tinyusb` 2.3.0 and TinyUSB 0.21.0~2 locked in `dependencies.lock`:

```powershell
eim run 'idf.py -B build/reliability-clean -D SDKCONFIG=build/reliability-clean/sdkconfig build' v5.5.5
```

Flash the board on COM3:

```powershell
eim run 'idf.py -B build/reliability-clean -p COM3 flash' v5.5.5
```

To enter download mode, hold BOOT, tap RESET, then release BOOT. After flashing, this board may need one RESET tap without BOOT. The running application enumerates as a USB mass-storage device rather than a flashing serial port.

## Windows migration from WebDAV

After flashing the USB MSC firmware and letting the drive enumerate, preview the migration:

```powershell
powershell -ExecutionPolicy Bypass -File tools/migrate_to_usb_msc.ps1 -DriveLetter S -WhatIf
```

Mapped drives can be hidden across Windows UAC sessions. From the PowerShell session that shares drive mappings with Explorer, prepare the user session and approve removal of the exact legacy `S:` mapping. Use an unelevated session when UAC is enabled; when UAC is disabled, the same administrator session can run both steps:

```powershell
powershell -ExecutionPolicy Bypass -File tools/migrate_to_usb_msc.ps1 -DriveLetter S -PrepareUserSession
```

Within ten minutes, run the administrative migration from an elevated PowerShell session and approve its prompts:

```powershell
powershell -ExecutionPolicy Bypass -File tools/migrate_to_usb_msc.ps1 -DriveLetter S
```

The preparation step removes `S:` only when it is the legacy `\\192.168.77.1@80\DavWWWRoot` mapping. The administrative step requires proof that the Explorer session was prepared, restores the backed-up `FileSizeLimitInBytes` value without stopping WebClient, and assigns `S:` to the USB device with VID `303A` and PID `4002`. The script refuses to replace any other mapping or local volume. Re-run the preparation step before a later migration attempt. A different target letter still cleans up only the legacy `S:` mapping.

Earlier scanner, MSC, NCM, and WebDAV experiments and their measured hardware results remain in [protocol notes](docs/scanner-protocol-notes.md). The legacy `tools/probe_es60w.ps1` requires `-InterfaceAlias`, `-ScannerSsid`, and `-ScannerPassword`. On the debug PC, select the TP-Link adapter with `-InterfaceAlias 'Wi-Fi 2'`. The probe restores that adapter's original connection, preserves existing profiles, and removes only its unique temporary profile and credential XML. Its native output parser expects English Windows field labels.

## References

- [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47)
- [Epson ES-60W specifications](https://files.support.epson.com/docid/cpd5/cpd56105/source/scanners/source/specifications/references/ds70_ds80w_es50_es65wr/spex_general_scanner_ds70_es65wr_r1.html)
- [ESP-IDF TinyUSB mass-storage example](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/device/tusb_msc/README.md)
