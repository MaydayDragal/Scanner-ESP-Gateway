# Scanner ESP Gateway

Firmware for a Waveshare ESP32-S3 1.47-inch display board to collect scans from an Epson WorkForce ES-60W, save them to microSD, and present completed scans to a computer as a USB mass-storage drive.

## Intended connection

```text
ES-60W -- Wi-Fi Direct --> ESP32-S3 -- microSD --> USB mass storage --> computer
```

The board has one USB data connection. The ESP32-S3 cannot use that connection as a USB host for the scanner and a USB mass-storage device for the computer at the same time. Scanner control over Wi-Fi Direct is the first feasibility gate; no wireless protocol is assumed yet.

## Quality target

- Request 600 dpi optical resolution and 24-bit color.
- Preserve lossless scanner output if available; otherwise save the scanner's original stream without adding another lossy encode.
- Stream scan data to the card because a full-resolution page can exceed available RAM.
- Use a temporary filename until a scan is complete and verified.

## Storage ownership

The ESP and the computer must not write or mount the same FAT volume simultaneously. The planned workflow is to temporarily remove the USB drive while the ESP writes a scan, then reconnect it after the file is closed and the card is unmounted. The computer-facing drive should be read-only.

## First milestones

1. Confirm the exact board revision and microSD wiring.
2. Connect to the ES-60W in Wi-Fi Direct mode and identify a scan control protocol and image format.
3. Bring up the board, microSD, and USB mass storage using ESP-IDF.
4. Implement streaming scan capture and safe USB drive handoff.
5. Verify 600 dpi output, file integrity, interrupted scans, and card-full behavior on the actual hardware.

## Current prototype

`main/main.c` is the first board milestone: it initializes the original USB-A board's four-bit microSD connection and exposes the card to the computer using TinyUSB mass storage. It does not scan yet. This prototype gives the computer write access to the card; the planned ownership handoff and read-only host behavior are still to be implemented. It does not format the card.

Build and flash with ESP-IDF v5.5.5:

```powershell
idf.py build
idf.py -p COM3 flash
```

Run `tests/usb_msc_smoke.ps1` on Windows to check that the USB disk enumerates.

After flashing this USB device firmware, the board's original USB Serial/JTAG COM port may disappear while the application runs. To flash again, hold **BOOT**, tap **RESET**, release **BOOT**, then use the new COM port in `idf.py -p PORT flash`.

## References

- [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47)
- [Epson ES-60W specifications](https://files.support.epson.com/docid/cpd5/cpd56105/source/scanners/source/specifications/references/ds70_ds80w_es50_es65wr/spex_general_scanner_ds70_es65wr_r1.html)
- [ESP-IDF TinyUSB mass-storage example](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/device/tusb_msc/README.md)
