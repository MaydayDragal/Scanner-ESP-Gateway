# Scanner ESP Gateway

Firmware for a Waveshare ESP32-S3 1.47-inch display board to collect scans from an Epson WorkForce ES-60W, save them to microSD, and present completed scans to a computer as a USB mass-storage drive.

## Intended connection

```text
ES-60W -- Wi-Fi Direct --> ESP32-S3 -- microSD --> USB mass storage --> computer
```

The board has one USB data connection. Scanner control uses the ES-60W's verified ESC/I-2 protocol over Wi-Fi Direct, leaving USB available for the computer.

## Quality target

- Request 600 dpi optical resolution and 24-bit color.
- Preserve lossless scanner output if available; otherwise save the scanner's original stream without adding another lossy encode.
- Stream scan data to the card because a full-resolution page can exceed available RAM.
- Use a temporary filename until a scan is complete and verified.

## Storage ownership

The ESP and the computer must not mount the same FAT volume simultaneously. This prototype scans before starting USB. After closing the scan and status files, it unmounts the card and exposes a read-only USB drive. It makes no further filesystem writes while USB is active. Automatic remounting on USB disconnect is disabled.

## First milestones

1. Confirm the exact board revision and microSD wiring.
2. Connect to the ES-60W in Wi-Fi Direct mode and identify a scan control protocol and image format.
3. Bring up the board, microSD, and USB mass storage using ESP-IDF.
4. Implement streaming scan capture and safe USB drive handoff.
5. Verify 600 dpi output, file integrity, interrupted scans, and card-full behavior on the actual hardware.

## Current prototype

The firmware initializes the original USB-A board's four-bit microSD connection, joins the scanner's Wi-Fi Direct network, and attempts one scan at startup. It requests 600 dpi RGB at scanner JPEG quality 100, streams image data through a 4 KB RAM buffer, and writes `SCAN0001.TMP` (then the next unused number). A complete transfer requires page-end and job-end tokens plus JPEG start/end markers. The file becomes `.JPG` only after flushing and closing it. Failures retain `.TMP` and are recorded in `GATEWAY.TXT`. The card is never formatted by the firmware.

The full ESP path was verified on 2026-09-18: the board captured `SCAN0005.JPG` (45,249,927 bytes) to microSD, reported successful scanner release, and exposed the card as read-only USB storage. The file was copied through USB and fully decoded on the PC as 5100 × 8400 RGB with 600 dpi metadata and quality-100 quantization tables. The IDF build, host protocol tests, and USB enumeration check passed. Actual card-full and power-loss recovery remain to be tested on hardware; protocol tests simulate write failure and truncated transfers.

### Use this milestone

1. Power the scanner in Wi-Fi Direct mode and load one page. Disconnect other scanner clients during the ESP test.
2. Plug in or reset the ESP. It attempts one scan before the USB drive appears. Leave it powered while scanning; the transfer deadline is six minutes, with bounded network timeouts.
3. When the read-only drive appears, open `GATEWAY.TXT` and copy the completed `.JPG` to the computer. `.TMP` files are incomplete and are never presented as successful scans.
4. Safely eject the drive before resetting the ESP for another page. To remove files from the read-only drive, use the SD card in a separate reader.

This first milestone uses an 8.5 × 14 inch acquisition canvas. The scanner may report a shorter actual page height; automatic cropping, long documents, multi-page jobs, and a scan button while USB remains connected are not implemented. JPEG quality 100 is still lossy, but the original scanner bytes are preserved without another encode. The firmware checks transfer completion and JPEG boundary markers; full decoding is part of the PC acceptance test.

Before building Wi-Fi support, copy `main/scanner_wifi_local.h.example` to `main/scanner_wifi_local.h` and enter the SSID and password printed on the scanner's label. The local file is ignored by Git. A build without it still works as a USB drive and reports Wi-Fi as unconfigured. The ESP stores the Wi-Fi configuration in RAM while running; the built firmware image contains the credentials and should be treated as private.

Build and flash with ESP-IDF v5.5.5:

```powershell
idf.py build
idf.py -p COM3 flash
```

Run `tests/usb_msc_smoke.ps1` on Windows to check that the USB disk enumerates.

To check the scanner's Wi-Fi Direct network and candidate scan services from a Windows computer, run `tools/probe_es60w.ps1` with its SSID and password as parameters. The script first confirms the SSID is visible, then temporarily joins it, probes common scanner ports and eSCL endpoints, reconnects the previous Wi-Fi network, and removes the temporary scanner profile. Do not save the password in this repository.

For the current two-adapter PC setup, use the TP-Link `Wi-Fi 2` interface explicitly. Do not use the older single-adapter probe script during debugging because it disconnects the built-in adapter. See [protocol and network notes](docs/scanner-protocol-notes.md) for the confirmed scan sequence and Windows connection settings.

Run the portable C protocol tests using `tests/run_protocol_tests.ps1 -Python <python.exe>`. That Python environment needs `ziglang` installed (`python -m pip install ziglang`). The tests exercise fragmented TCP reads/writes, empty frames, 256 KB image blocks, malformed/truncated responses, missing completion markers, and simulated SD write failure.

Validate a copied hardware scan with `python tests/verify_scan.py path/to/SCAN0001.JPG` (requires Pillow). This fully decodes the file and checks color mode, dimensions, resolution metadata, and quality tables.

After flashing this USB device firmware, the board's original USB Serial/JTAG COM port may disappear while the application runs. To flash again, hold **BOOT**, tap **RESET**, release **BOOT**, then use the new COM port in `idf.py -p PORT flash`.

## References

- [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47)
- [Epson ES-60W specifications](https://files.support.epson.com/docid/cpd5/cpd56105/source/scanners/source/specifications/references/ds70_ds80w_es50_es65wr/spex_general_scanner_ds70_es65wr_r1.html)
- [ESP-IDF TinyUSB mass-storage example](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/device/tusb_msc/README.md)
