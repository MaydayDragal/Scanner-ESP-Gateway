# ES-60W network protocol observations

These observations were made with an ES-60W in Wi-Fi Direct mode. They are evidence for the next firmware milestone, not a working scan implementation.

- The scanner assigned the Windows client an address on `192.168.223.0/24`; its gateway and service address was `192.168.223.1`.
- TCP ports 80 and 1865 accepted connections. Ports 443, 3289, 5357, and 8080 did not. HTTP GET requests to `/`, `/eSCL/ScannerCapabilities`, and `/eSCL/ScannerStatus` returned 404. Port 3289 was tested over TCP only; Epson discovery commonly uses UDP.
- The first TCP/1865 connection sent `49 53 80 00 10 0C 00 00 00 05 00 00 01 04 00 00 00` without a request. `49 53` is the ASCII `IS` frame marker and `80 00` is a welcome frame type. This is consistent with Epson's plain TCP ESC/I-2 transport, but the command set still needs confirmation on this model.
- An IS `0x2100` lock request with the seven-byte payload used by SANE's `epsonds` backend received an IS `0xA100` response containing `06` (ACK).
- Sending `INFOx0000000` immediately after that lock produced no response before timeout. The SANE backend performs additional initialization before `INFO`; the missing step needs verification on this scanner. Subsequent connections returned a longer welcome frame containing the client's IP address and closed on a lock request. The meaning of that longer frame is not yet confirmed. Further command probes should avoid leaving a session locked.

The Windows computer's Wi-Fi scan did not reliably show the scanner while associated with a 5 GHz network. Disconnecting it temporarily and scanning again revealed the scanner SSID. The probe script restores the original network and removes its temporary scanner profile.

## Dedicated scanner adapter (2026-09-18)

The Windows PC now has a TP-Link Wireless USB Adapter named `Wi-Fi 2`. Its scanner profile is set to connect automatically, its preferred band is 2.4 GHz, and its IPv4 and IPv6 interface metrics are 500. The built-in `Wi-Fi` adapter retains the internet connection. `Find-NetRoute` verified that `192.168.223.1` uses `Wi-Fi 2`, while internet traffic uses `Wi-Fi`. A scanner TCP connection and an HTTPS request to GitHub both succeeded with both adapters connected.

For subsequent PC debugging, target `interface=Wi-Fi 2` explicitly. Do not run the original single-adapter probe script: it disconnects the built-in adapter. The ESP was unplugged for this test; simultaneous PC/ESP scanner access has not been verified.

## Confirmed command initialization and capabilities (2026-09-18)

The sequence IS job lock (`0x2100`), passthrough `FS X` (`1C 58`, one-byte ACK), then `INFO`, `CAPA`, and `RESA` succeeded. `FIN ` followed by IS job unlock (`0x2101`) released the capability-query session, and a subsequent session opened successfully.

The device reported:

- Model ES-60W, firmware ADF 1.20.
- Color mode `C024` (24-bit RGB).
- Main and sub-scan resolutions 200, 300, 400, and 600 dpi.
- Transfer format `JPG ` only; JPEG quality range 1–100.

`PARA` accepted 600 dpi in both axes, `C024`, and JPEG quality 100. `TRDT` began a scan. A passthrough write with zero expected response can return an empty IS frame that must be consumed before the command reply. The first image header announced 262,144 bytes despite a requested 65,536-byte transfer buffer. Its JPEG header recorded 600 dpi. The first transfer test stopped at the reader's smaller buffer limit; complete image capture is still pending. A subsequent session received NAK for `FS X`, so recovery from an interrupted image transfer also needs implementation and testing.

The ESP implementation should stream image blocks to SD in smaller chunks rather than allocating an entire announced block. JPEG quality 100 remains lossy; wrapping or decoding it as TIFF/PDF cannot recover detail absent from the scanner's network output.

The next PC transfer received 30,408,704 JPEG bytes at 5100 × 8400 pixels, with 600 dpi metadata, before a disconnect. Pillow rejected the partial image as truncated and no JPEG end marker was present. Windows logged WLAN AutoConfig event 4003 (limited-connectivity automatic recovery), followed by TP-Link disconnection event 8003 at the same timestamp. Thus this attempt also exposed a PC Wi-Fi interruption, not a completed scan. Temporarily disabling autoconfiguration on `Wi-Fi 2` preserved its existing connection in a check; re-enable it before reconnecting or restarting the scanner. Its effectiveness during a full scan is still pending verification.

## Successful PC scan (2026-09-18)

Disabling autoconfiguration did not prevent a subsequent interruption. Disabling IPv6 on the dongle also did not resolve the connection problem; IPv6 and autoconfiguration were restored. Windows subsequently logged a temporary-disconnect request. The previously unset DWORD `fMinimizeConnections` was set to `0` under `HKLM\SOFTWARE\Policies\Microsoft\Windows\WcmSvc\GroupPolicy` to permit simultaneous connections. This is a machine-wide setting; remove that value to restore the previous default. See [Microsoft's connection-manager policy documentation](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-admx-wcm).

After this change, a complete scan succeeded while the built-in adapter retained internet access:

- Requested 600 × 600 dpi, 24-bit RGB, JPEG quality 100, 5100 × 8400 pixel acquisition area, and 262,144-byte blocks.
- Received 45,846,054 bytes, a JPEG end marker, and `#peni0005100i0006648#lftd000` from the scanner.
- Pillow decoded the entire JPEG successfully: RGB, 5100 × 8400 pixels, 600 dpi metadata. Both quantization tables contained only ones.
- The image canvas retains the requested height of 8400 pixels, while the scanner's page-end token reports 6648 pixels. Automatic page cropping remains future work.
- `FIN ` completed successfully, job unlock was sent, and a subsequent capability-query session completed. An HTTPS request through the internet connection also succeeded.

The test image and diagnostic script remain in ignored `backups/`. This verifies PC-to-scanner transfer; the current ESP firmware still only connects, writes its boot report, and exposes SD over USB. Porting the working scan sequence to the ESP and validating SD storage are pending.

## Verified ESP-to-SD capture (2026-09-18)

The firmware now uses a single scanner TCP session after Wi-Fi association. It requests the same proven settings, receives each scanner block in 4096-byte pieces, checkpoints the file every MiB, and requires both page/job completion plus JPEG boundary markers before flushing, closing, and renaming TMP to JPG. An SD write failure drains the current network frame before attempting CAN/FIN/unlock; a host regression test covers this cleanup.

Hardware result: `SCAN0005.JPG`, 45,249,927 bytes, scanner session released successfully. The lowest logged main-task stack headroom during capture was 848 bytes. The USB drive appeared after capture; Windows reported it read-only. A copy made through USB passed full Pillow decoding at 5100 × 8400 pixels, RGB, 600 dpi, with all-one JPEG quantization tables. This verifies scanner → ESP Wi-Fi → microSD → USB → PC for one page.

Earlier interrupted attempts remain as TMP files. This milestone starts one scan per boot, keeps USB unavailable during capture, and does not yet implement automatic page cropping or repeated scans without resetting. Physical card-full and interrupted-power tests remain outstanding; mocked transfer/write failures are covered in the portable C tests.

## Source references

- [Epson's documented network scan service on TCP/1865](https://files.support.epson.com/docid/cpd6/cpd60230.pdf)
- [SANE ESC/I-2 network framing and lock request](https://gitlab.com/sane-project/backends/-/blob/master/backend/epsonds-net.c)
- [SANE initialization and command order](https://gitlab.com/sane-project/backends/-/blob/master/backend/epsonds.c)
- [Independent IS framing and ESC/I-2 protocol notes](https://github.com/mtheuma/epson2paperless/blob/main/docs/PROTOCOL-REFERENCE.md)
