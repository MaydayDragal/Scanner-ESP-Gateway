# Scanner ESP Gateway

Firmware for a Waveshare ESP32-S3 1.47-inch display board that collects scans from an Epson WorkForce ES-60W, saves them to microSD, and presents the card to Windows through USB mass storage.

Reliability software changes on this branch passed host tests, independent review and a fresh firmware build. The new storage modes, original preservation and recovery behavior still require hardware acceptance. See the [release evidence and remaining checks](docs/qa/reliability-acceptance.md).

## Active connection

```text
ES-60W -- Wi-Fi Direct --> ESP32-S3 -- microSD --> USB MSC --> Windows
```

The board has one USB data connection. Scanner control uses the ES-60W's verified ESC/I-2 protocol over Wi-Fi Direct, leaving USB available for mass storage.

## Automatic scan cycle and storage ownership

The microSD FAT volume has one owner at a time. In normal automatic mode, Windows has read-only USB access while the gateway is idle. When the feeder detects a new page, the firmware quiesces and disconnects USB, mounts the card on the ESP, saves the scan, unmounts the card, and reconnects USB.

The drive disappears during each scan and reappears afterward. Finish a Windows copy before feeding a page if that copy must complete uninterrupted. The firmware never formats the card or overwrites an existing scan or working file.

Writable maintenance mode permits Windows to add, edit, rename or delete files and suspends automatic scanning. With the display awake and the gateway idle, hold BOOT for two seconds to select maintenance. After finishing file operations, safely eject the drive in Windows, then hold BOOT for two seconds to request automatic mode. The ESP requires confirmed host release; unplugging, resetting or waiting cannot substitute for eject. A failed release stays blocked. The exact Windows eject sequence remains a hardware acceptance check.

A short BOOT press while awake and idle acknowledges the retained scan alert. It does not clear a storage fault. A gesture that starts with the display asleep only wakes it. Release BOOT and make a new gesture to change mode. Gestures begun during scanning or a storage transition cannot select a mode. BOOT remains an input; the normal hold-BOOT/tap-RESET download procedure is unchanged.

An unmountable filesystem may be exposed through read-only recovery when raw card access and ownership are safe. A missing card or uncertain USB state leaves the gateway stopped. Recovery does not automatically format or repair the card.

After reconnecting, firmware waits up to five seconds for USB host configuration before completing the handoff. If no host is detected, it completes local USB ownership and keeps MSC ready for a later connection; the log reports that the host is unconfigured. Host configuration does not prove Windows has finished mounting the volume.

Successful originals use the next available `SCANnnnn.JPG` name. If a narrower crop is proposed, it is saved separately as `CROPnnnn.JPG` after the original is safely published. The original retains the full acquisition width. Interrupted or invalid output remains as a `.TMP` file and does not count as a completed scan. Number allocation checks both original and derivative names, including legacy `.CRP` files, before creating an exclusive scratch file. Insert successive pages without resetting the ESP. After a failed scan, remove the page briefly before reloading it. Keep the scanner charging because low-battery status is recognized alongside paper status.

## Scan settings and interface

Current settings are **300 dpi RGB, scanner JPEG quality 75**, using a 2550 x 4200 acquisition canvas. The scanner's JPEG bytes are received and saved in **16 KiB chunks** without another encode. The ESP validates every encoded row, checks the scanner's page-end dimensions, and normalizes the original's height before publication. An optional right-edge derivative copies retained compressed JPEG data without recompressing it. Dark edge content can resemble the scanner background, so the full-width original is retained. Original validation failure leaves a `.TMP`; derivative failure keeps the saved original and reports a crop warning.

The onboard 320 x 172 landscape display shows scanner connection, feeder and low-battery status, the current action and active scan settings. Current activity is separate from the retained scan result. Received bytes during capture are distinct from the published file size. Failed scans and scanner-cleanup/crop warnings remain available through later polling. Visual initialization or transfer errors are retained; safe scanning can continue headless.

After five minutes without a page or meaningful status change, the display and status LED turn off. The gateway keeps checking the feeder and wakes the indicators on new activity or BOOT input. Failed off transitions are retried at one-second intervals, at most three times, with display and LED confirmation tracked separately. Active acquisition, capture, finalization and restoration suppress sleep.

After an LCD transfer timeout, the firmware still attempts to turn the backlight off. It avoids further panel commands that could wait indefinitely for the unfinished transfer, retains the in-flight buffer, and reports the panel state as unconfirmed. Rendering then remains disabled until restart. A failed visual wake keeps BOOT gestures limited to wake requests; it cannot silently authorize a storage mode change.

The onboard RGB LED uses white for startup, yellow for reconnecting/unavailable, green for ready, cyan for loaded paper, blue for scan activity, orange for warnings/maintenance/time synchronization, and red for retained failure or stopped storage. An unacknowledged outcome can take priority over current readiness. This board uses RGB wire order; green at READY was verified on the earlier firmware. LED failure does not stop the gateway.

## Verification

- Install the pinned host test packages: `python -m pip install -r tests/requirements.txt` (Pillow 12.3.0 and ziglang 0.16.0).
- Host protocol, storage lifecycle, image-verifier, and JPEG crop tests: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1` (or add `-Python <python.exe>`). The runner works in a clean checkout with no `build` directory.
- USB MSC hardware check: `powershell -ExecutionPolicy Bypass -File tests/usb_msc_smoke.ps1`.
- Image validation: `python tests/verify_scan.py image.JPG --dpi 300 --quality 75` checks full decode, RGB, 300 dpi metadata, page dimensions and the registered quality-75 tables for the current profile. Use `--dpi 600` for earlier 600 dpi scans and choose the matching quality. Omitting `--quality` reports **quality unverified**. Explicit quality verification succeeds only for a registered Epson quantization-table set. Complete quality-50 and quality-75 tables are in `tests/fixtures/epson_quantization.json`; quality 75 was calibrated from a known ES-60W scan during hardware QA. Quality 100 is not registered in that verifier; the scanner's documented quality-100 output has two complete quantization tables containing only ones. See [protocol notes](docs/scanner-protocol-notes.md) and the [acceptance record](docs/qa/reliability-acceptance.md) for provenance and remaining checks. Synthetic test JPEGs are not evidence of scanner output.
- CI runs the host suite and a clean ESP-IDF 5.5.5 build with job-local dummy Wi-Fi values. It does not publish firmware binaries.

Historical hardware verification on 2026-09-18 covered the earlier writable-idle firmware: Windows enumerated a local FAT32 USB disk at `S:`; creation, readback, rename, deletion and one flushed-file scan handoff passed. A later session found FAT errors. Those results do not validate this reliability candidate. See [protocol notes](docs/scanner-protocol-notes.md) for the chronology and the [acceptance matrix](docs/qa/reliability-acceptance.md) for current status.

## Build and flash

Copy `main/scanner_wifi_local.h.example` to ignored `main/scanner_wifi_local.h` and enter the SSID and password printed on the scanner label. Firmware binaries contain these credentials and must remain private.

To timestamp new scans, add `TIME_WIFI_SSID` and `TIME_WIFI_PASSWORD` for a 2.4 GHz home network to that ignored header. At startup the ESP briefly connects to home Wi-Fi, attempts Internet time synchronization, then reconnects to the scanner. An unknown clock shows `TIME NOT SET`, including before the first scan; clock validity and any synchronization error are also retained with the scan result. A failed retry does not erase a previously valid clock. FAT file dates use Eastern time with daylight saving. Existing files keep their original timestamps.

Build using ESP-IDF v5.5.5, with `esp_tinyusb` 2.3.0, TinyUSB 0.21.0~2 and `led_strip` 3.0.3 locked in `dependencies.lock`. The two local component overrides carry their upstream licenses, hashes and patch notes. Configure rebases their generated local lock paths to the current checkout without changing the version pins:

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
