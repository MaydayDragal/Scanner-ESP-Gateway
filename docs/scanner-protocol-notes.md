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

The firmware now uses a single scanner TCP session after Wi-Fi association. It requests the same proven settings, receives each scanner block in 16 KiB pieces, reports progress every MiB, and requires both page/job completion plus JPEG boundary markers before flushing, synchronizing, closing, and renaming TMP to JPG. Avoiding intermediate filesystem synchronizations keeps the scanner stream moving; an interrupted capture remains a private TMP file. An SD write failure drains the current network frame before attempting CAN/FIN/unlock; a host regression test covers this cleanup.

Hardware result: `SCAN0005.JPG`, 45,249,927 bytes, scanner session released successfully. The lowest logged main-task stack headroom during capture was 848 bytes. The USB drive appeared after capture; Windows reported it read-only. A copy made through USB passed full Pillow decoding at 5100 × 8400 pixels, RGB, 600 dpi, with all-one JPEG quantization tables. This verifies scanner → ESP Wi-Fi → microSD → USB → PC for one page.

Earlier interrupted attempts remain as TMP files. This milestone starts one scan per boot, keeps USB unavailable during capture, and does not yet implement automatic page cropping or repeated scans without resetting. Physical card-full and interrupted-power tests remain outstanding; mocked transfer/write failures are covered in the portable C tests.

## Source references

- [Epson's documented network scan service on TCP/1865](https://files.support.epson.com/docid/cpd6/cpd60230.pdf)
- [SANE ESC/I-2 network framing and lock request](https://gitlab.com/sane-project/backends/-/blob/master/backend/epsonds-net.c)
- [SANE initialization and command order](https://gitlab.com/sane-project/backends/-/blob/master/backend/epsonds.c)
- [Independent IS framing and ESC/I-2 protocol notes](https://github.com/mtheuma/epson2paperless/blob/main/docs/PROTOCOL-REFERENCE.md)

## Automatic page detection (2026-09-18)

A read-only PC probe compared the same ES-60W with an empty and loaded feeder. After lock and FS X, `STATx0000000` returned `#nrdNONE` in both cases. Empty: 12-byte payload `#ERRADF PE  `. Loaded: zero-byte payload. Both sessions closed with FIN and job unlock. Firmware accepts only these observed states; unexpected payloads, busy/error replies, and cleanup failures produce unknown status and cannot trigger a scan.

The repeat loop debounces two loaded samples, keeps each page in a separate numbered JPEG, and disarms after failure until an empty feeder is observed. Successful page/job completion permits the next page after a cooldown. USB ownership uses a deferred callback on the TinyUSB task to disconnect and park it before driver teardown; this relies on the pinned dependency version in dependencies.lock and must be rechecked when upgrading TinyUSB. Hardware verification succeeded: the user confirmed repeated page insertions with no resets, producing SCAN0006.JPG through SCAN0008.JPG. Two files were copied from USB and fully decoded: SCAN0007.JPG, 49,238,719 bytes, SHA-256 61DB22F4172FCCEDAFA89AD7D6C064A40CEBD6F65311402F1A703A8BB0902413; SCAN0008.JPG, 46,752,123 bytes, SHA-256 05380740636E245A64FD4980EE1A29E3F674A3E8DC3A44C3769A6589F82132C0. Both are 5100 ? 8400 RGB, 600 dpi, with all JPEG quantization entries equal to 1. Windows reported the disk read-only. No new files appeared while the feeder stayed empty during copying. The user inserted additional pages before the requested second-page check; this explains why three completed files were already present when USB was inspected. Active host-copy interruption and real card-full/power-loss recovery remain untested.

### Low-battery status correction

After switching to 300 dpi, a loaded page did not trigger scanning. A PC probe found STAT ready (#nrdNONE) with the eight-byte payload `#BATLOW `, which the strict parser previously rejected. The parser now permits this battery warning alongside the known paper-empty token, in either order. Unknown tokens and other errors still suppress scanning. Regression fixtures reproduce the failure and verify loaded, empty, and mixed error cases. The scanner remained on USB power during follow-up testing.

Hardware follow-up passed: after a scanner-only restart resolved Wi-Fi reconnection, the running ESP reconnected without being reset and saved SCAN0009.JPG (12,673,748 bytes). The USB copy fully decoded as 2550 x 4200 RGB at 300 dpi with quality-100 tables. GATEWAY.TXT reported complete capture and successful scanner release.

## Continuous USB mapped drive (2026-09-18)

Firmware now uses NCM USB networking (VID 303A, PID 4003) and permanently mounts FATFS on the ESP. Windows USB adapter index 44 was configured to 192.168.77.2/24, no gateway; device is 192.168.77.1. WebClient maps S: to the read-only WebDAV root. Existing scanner Wi-Fi and internet interfaces remain separate. The previous MSC firmware is preserved privately in backups/working-300dpi-msc.bin.

Protocol smoke checks passed on hardware: persistent HEAD followed by PROPFIND, directory listing, hidden TMPs, rejected PUT/DELETE, traversal rejection, and byte ranges. Initial address gating needed correction for IPv4-mapped IPv6 socket addresses. USB-local and fixed host address checks now accept the intended connection.

Measured copies through S:: 12,673,748 bytes in 13.19 s (0.961 MB/s); 46,752,123 bytes in 48.51 s (0.964 MB/s), SHA-256 matching the earlier source image. These are observed rates, not a controlled comparison with MSC. A new SCAN0012.JPG (12,694,650 bytes) appeared without reconnecting and fully decoded as 2550 x 4200 RGB, 300 dpi, quality-100 tables. Strictly timed overlap verification remains pending.

Capture buffering follow-up: scanner receive and SD writes now use 16 KiB chunks, with intermediate per-MiB filesystem synchronizations removed. The completed file is still flushed and synchronized before TMP is renamed to JPG. Hardware produced SCAN0020.JPG (12,688,651 bytes) in 22.16 seconds as observed through the USB status endpoint; the copied file fully decoded as 2550 x 4200 RGB, 300 dpi, with quality-100 tables. Direct USB WebDAV copy measured 0.945 MB/s, confirming no material change to the separate full-speed USB bottleneck. No controlled capture-duration baseline was recorded, so a capture speedup cannot yet be quantified.

JPEG quality was then reduced to 50. The protocol regression test confirms `#JPGd050` in the outgoing PARA payload, and the display and status file report the same setting. Hardware produced SCAN0021.JPG through SCAN0023.JPG at about 1.33 MiB each. SCAN0023.JPG was 1,345,854 bytes, copied over WebDAV in 1.49 seconds, fully decoded at 2550 x 4200 RGB and 300 dpi, and used Epson's observed standard quality-50 quantization tables. Compared with the 12,688,651-byte quality-100 SCAN0020.JPG, this sample is 89.4% smaller; page contents were not controlled, so this is an operational comparison rather than a compression benchmark.

Follow-up: the status endpoint confirmed an active scan before copying SCAN0007.JPG through S:. The 49,238,719-byte copy completed in 62.75 seconds and matched its original SHA-256. Repeated requests then exposed idle WebClient connections exhausting HTTP server slots (default LRU purge was off). The firmware now enables idle-session LRU purge and is flashed; all WebDAV smoke checks plus 16 held idle clients pass against the corrected firmware.

Windows recovery remains pending: restarting WebClient after the original stall left its dedicated svchost PID 31276 stuck in STOP_PENDING with one kernel-blocked thread. Termination was requested, but it has not exited; MRxDAV cannot stop while that dependent service is pending. Some PowerShell processes also block on the stale mapped drive. Native cmd.exe tools and direct HTTP to the corrected ESP continue working. A Windows restart is needed before rechecking S:. No restart was performed automatically. The USB network adapter settings persist (index 44, host 192.168.77.2/24); mapping target is \\192.168.77.1@80\DavWWWRoot. Background restart/delete commands were stopped to avoid delayed actions. After restart, start WebClient and restore S: if necessary, rerun tests/webdav_smoke.py, enumerate/copy through S:, then update this recovery note.

## Migration back to automatic read-only USB MSC (2026-09-18)

At this milestone, firmware moved from NCM/WebDAV back to USB mass storage with exclusive microSD ownership. Windows owns the FAT volume only while the gateway is idle. On a debounced page trigger, the firmware disconnects TinyUSB, transfers the volume to the application, captures and closes the scan in 16 KiB chunks, unmounts the application filesystem, transfers the volume back to USB, and reconnects TinyUSB. No application filesystem access is allowed while USB owns the card. Failed transitions revoke application access, and recovery is allowed to restore USB ownership only.

That firmware exposed one read-only MSC interface with VID `0x303A` and PID `0x4002`. NCM, the USB HTTP server, and WebDAV were excluded from the build. The existing scanner behavior remained 300 dpi RGB, scanner JPEG quality 50, automatic page detection, the display status model, and the LED state mapping.

Hardware verification on 2026-09-18 used the 940,160-byte firmware image with SHA-256 `DDA3AFFCACFEA901F9E0B7F937712F3ECBE1DC3818E2DB17E2019ABD55388CFD`. Flashing on COM3 completed with verified hashes; one normal RESET started the application. Windows enumerated `VID_303A&PID_4002` as a TinyUSB USB mass-storage device, a 4 GB FAT32 disk, and reported `IsReadOnly=True`. Existing `SCAN0005.JPG` through `SCAN0026.JPG` remained visible. A file-creation attempt on the USB volume failed with “The media is write protected.”

The legacy WebDAV `S:` mapping was removed by the migration script. The backed-up WebClient `FileSizeLimitInBytes` value of 50,000,000 was restored, and the scanner's local USB partition received `S:`. This host has UAC disabled, so preparation and assignment ran in the same administrator session. A migration script correction permits that configuration while retaining the unelevated Explorer-session requirement when UAC is enabled. The script's `-WhatIf` query now identifies the MSC disk without modifying it.

Two successive automatic scans completed without an ESP reset. `SCAN0027.JPG` was 1,316,946 bytes with a 4,650 ms capture; `SCAN0028.JPG` was 1,316,074 bytes with a 5,178 ms capture. Both fully decoded as 2550 x 4200 RGB, 300 dpi, using quality-50 quantization tables. `GATEWAY.TXT` reported scanner TCP/1865 open, scan complete, and scanner session released for both. During the second cycle, polling observed `S:` disappear at 22:26:06.789 and reappear with `SCAN0028.JPG` at 22:26:13.396, a 6.607-second absence. The drive retained letter `S:` after reconnecting. SHA-256: `SCAN0027.JPG` `1736B01B59250007BABDD622CEF9B6629DD2585ECDAD8AE03CCF5F4E764FA467`; `SCAN0028.JPG` `4DE6B647550EB415AFFC306084DC8A2127A9010D5DB8D026C186AD8D35737E8F`.

An active Windows copy across the transition, real card-full handling, and interrupted-power recovery were not physically tested on this firmware revision.

## Writable USB MSC while idle (2026-09-18)

The USB MSC write-protection callback now advertises write access only for LUN 0 while storage is assigned or being assigned to USB. An atomic flag carries this decision across the main and TinyUSB tasks. It rejects writes while the ESP owns the card or a handoff has failed. The existing exclusive ownership transition, TinyUSB task quiescence, and no-format setting remain in place. Host lifecycle tests cover the callback in APP, USB, transition, and error states.

The 940,192-byte firmware image with SHA-256 `17331B413E2D6A5C30615560DDAA392361A065568C91D7F8A77E4D4E61B9A7F9` flashed on COM3 with verified hashes. After a normal RESET, Windows reported the same `VID_303A&PID_4002` local FAT32 disk at `S:` with `IsReadOnly=False`. A new Windows file was created and read back, renamed and read back again, then deleted. A second host-written file was flushed and closed before a page was inserted.

During the page scan, polling observed `S:` disappear at 22:37:42.384 and return at 22:37:48.455, a 6.071-second absence. The host-written file was unchanged after reconnecting and was then deleted. The ESP created `SCAN0029.JPG` (1,314,127 bytes, 4,678 ms capture), with a successful scanner session release. The USB JPEG fully decoded as 2550 x 4200 RGB, 300 dpi, with quality-50 tables; SHA-256 `F499423AA047B2087FD598CFD4DAA1688C18E05036A999BF082CEDDB4A839DA4`.

Windows writes that are still in progress when a page is inserted can be interrupted by the automatic USB disconnect. Finish copies and close files before feeding a page. A live host write across the handoff, card-full handling, and power-loss recovery remain untested.

The final atomic-flag build was flashed and verified: 940,304 bytes, SHA-256 `D88EF7E6AD54AD9EECB5D565DA71F05D0BF709BFB1A28C88956027801FA0301B`. After a normal RESET, Windows again showed local `S:` with `IsReadOnly=False`. A flushed host-written test file survived a scan unchanged and was removed after verification. `S:` disappeared at 22:41:18.856 and returned at 22:41:25.014, a 6.158-second absence. `SCAN0030.JPG` was 1,319,529 bytes, captured in 4,801 ms; `GATEWAY.TXT` reported a completed scan and scanner release. Full JPEG decoding passed at 2550 x 4200 RGB, 300 dpi, quality-50 tables. JPEG SHA-256: `71146DB667B2B885FCD3F24653F36158B2AC2CA81131F73DF35B1CB180344347`.

## Automatic page crop (2026-09-18)

`SCAN0030.JPG` showed a broad gray area after the paper: its JPEG header retained the requested 4200-pixel acquisition height, while its entropy stream contained only 3256 encoded rows. The scanner's page-end response provides the page dimensions. The ESP now validates that response and the JPEG's baseline frame, 160-MCU restart interval, restart sequence, and end marker before changing only the JPEG frame height in the private TMP file. The compressed image is not recompressed. A failed validation retains the TMP rather than publishing an incorrectly cropped JPG.

The first hardware build required the page-end height to be divisible by eight. One scan was rejected with `Page-end dimensions outside scan area`; `SCAN0031.TMP` was retained at 1,316,703 bytes. Its JPEG stream was complete and contained 3248 encoded rows. The page-end height need not align to an eight-pixel JPEG MCU boundary, so the validator was corrected to accept a page-end height within eight pixels of the encoded boundary. The actual page-end value from that failed scan was not logged. A temporary copy of `SCAN0031.TMP` cropped to 3248 rows fully decoded; the card's original TMP was left intact.

The corrected 942,304-byte firmware (SHA-256 `5DEF38291292316C63F27B59543C9B299CD43DC4056A30CC76CCB9CCE15E7076`) flashed on COM3 with verified hashes. A new page caused `S:` to disappear at 23:01:46.272 and return at 23:02:06.808. `GATEWAY.TXT` reported `Scan complete: yes`, scanner released, `SCAN0032.JPG`, 1,326,993 bytes, and 19,229 ms capture. The USB file fully decoded at 2550 x 3296 RGB, 300 dpi, with quality-50 tables; SHA-256 `55A07DA049B0709134BBA563593922B7C734991845182EAD7BF8434A58F8EDFB`. Visual inspection showed the large gray tail removed. The final few rows still show the paper-edge transition recorded by the scanner.

The first crop validator read the JPEG entropy stream one byte at a time from SD, adding about 14 seconds to this scan. It now reads 4 KiB blocks, including markers that span blocks. The final 942,496-byte firmware (SHA-256 `B69B805344B1CF2572720EFF0A2D4695C9D1AAFBAA8F46B8AF41088CE2FA38B6`) flashed on COM3 with verified hashes. A new page caused `S:` to disappear at 23:06:17.004 and return at 23:06:24.104, a 7.100-second absence. `GATEWAY.TXT` reported a completed scan, scanner release, `SCAN0033.JPG`, 1,309,755 bytes, and 5,705 ms capture. The JPEG fully decoded at 2550 x 3224 RGB, 300 dpi, quality-50 tables; SHA-256 `F7CACFA1C8E04103A101E4AF931AF4C560A0D8F99663C21B984C9401643D7936`. Visual inspection again showed no large gray tail. This single follow-up is an operational timing check, not a controlled benchmark.

## Onboard LED color order (2026-09-18)

The ESP display showed `READY TO SCAN` while its onboard RGB LED appeared red. The LED model requested green for this state and the gateway was otherwise waiting for paper, so the discrepancy was in the LED output mapping. The driver had selected GRB wire order. Changing it to RGB kept the logical status colors unchanged and made the physical LED green at `READY TO SCAN`, as confirmed by the user after flashing and resetting. The 942,496-byte image (SHA-256 `9D4EF0C0412E48B07E9268B3563F71379535631E70BF91BEA8657AD3CF9EE2D7`) flashed with verified hashes. After the normal reset, `S:` returned and `GATEWAY.TXT` reported Wi-Fi connected and waiting for paper.

## 600 dpi at JPEG quality 50 (2026-09-18)

The scan request now uses 600 dpi on both axes, quality 50, and a 5100 x 8400 acquisition area. Page-end validation accepts dimensions within that area. The lossless JPEG crop validator accepts either the earlier 2550 x 4200 / DRI 160 layout or the 600 dpi 5100 x 8400 / DRI 319 layout; it still requires the encoded height to agree with the scanner page-end value within eight pixels. A temporary copy of an earlier real 600 dpi JPEG was cropped to 5100 x 6528 and fully decoded before flashing.

The 942,608-byte firmware image (SHA-256 `852031998A5AD5AC16B899A60F33C610EE8A69DA7E98AFAF7CD80CB058F23124`) flashed on COM3 with verified hashes. After normal RESET, `GATEWAY.TXT` showed 600 dpi RGB and JPEG quality 50. A new page caused `S:` to disappear at 23:20:28.305 and return at 23:20:45.492, a 17.187-second absence. `SCAN0002.JPG` was 4,673,479 bytes with a 16,043 ms capture. The file fully decoded at 5100 x 6504 RGB, 600 dpi, with the observed Epson quality-50 quantization tables; SHA-256 `53B56A5E2839D5E7D7B0EE0FB00F4F6BD22FD1C15A28DC9F107FA2AF8CAA52CE`. Visual inspection showed no large gray tail. `GATEWAY.TXT` reported scan complete and scanner released. The earlier 300 dpi quality-50 scan was about 1.3 MB and took about 5.7 seconds; these are separate page samples, not a controlled benchmark.

## Automatic right-edge crop for mixed page widths (2026-09-18)

The 600 dpi narrow-page scans `SCAN0003.JPG` and `SCAN0004.JPG` retained the 5100-pixel acquisition width and showed a broad black right area beginning around pixel 3300. Patching only the JPEG width corrupts decoding because each restart row still contains the original MCU count. The ESP now detects a nearly uniform dark suffix from decoded JPEG luma DC coefficients, rewrites each restart row with only the retained MCU bitstream, and updates the frame width and restart interval. It copies the retained compressed coefficients without recompressing them. The crop writes a separate `.CRP` file and publishes it only after a complete, flushed output; invalid JPEGs retain the original `.TMP`. A full-width page without a dark right suffix passes through unchanged.

The corrected firmware flashed on COM3 with verified hashes. A narrow page produced `SCAN0008.JPG` (2,693,494 bytes, 26,104 ms capture), which fully decoded at 3360 x 5144 RGB, 600 dpi. Visual inspection showed the broad black right area removed; a thin dark paper-edge line remains. SHA-256: `A464B8F667B868EE691A81951DA255E5040225A3CCB713EF697A1EBD0682E5CF`. A subsequent full-width page produced `SCAN0009.JPG` (4,745,727 bytes, 18,622 ms capture), fully decoded at the unchanged 5100 x 6616 RGB, 600 dpi. SHA-256: `7D00872EBA3FD311D2682D0D5403950B4B996DF4723A38E4D3FD3F7DB0BE3306`. `GATEWAY.TXT` reported completed scans and scanner release for both. The narrow scan takes longer because the ESP reads the encoded rows and writes a second JPEG on the SD card; these page timings are not a controlled speed comparison.

## 300 dpi at JPEG quality 75 (2026-09-18)

The scan request now uses 300 dpi on both axes, scanner JPEG quality 75, and a 2550 x 4200 acquisition canvas. Page-end validation enforces that area. The existing height and automatic right-edge crops remain enabled. The protocol fixture checks the new parameter tokens, and the JPEG width-crop tests include a quality-75 image. The firmware build completed, and COM3 flashing verified the image hashes. Firmware SHA-256: `0F4B8050EE721A2E6E354122C89CFD3FE14AB85F450FB2FFD5CCA8139ED081DA`.

After normal RESET, `S:` returned and `GATEWAY.TXT` reported 300 dpi RGB, JPEG quality 75, a completed scan, and scanner release. `SCAN0010.JPG` was 2,074,524 bytes and captured in 9,229 ms. It fully decoded at 2550 x 3216 RGB with 300 dpi metadata. Its first luminance quantization values were `8, 6, 5, 8, 12, 20, 26, 31`, compared with `16, 11, 10, 16, 24, 40, 51, 61` at the earlier quality-50 setting; this confirms that the scanner applied the higher quality. JPEG SHA-256: `3EB36FBC5C7A91D9F2E00E5E012F95A0797D3346AD1F17355DCFA39446B7C69D`.

## Scan file timestamps (2026-09-18)

The ESP booted without a valid wall clock, so FatFs encoded 1980-era timestamps; in Eastern time Windows displayed `12/31/1979`. The scanner Wi-Fi Direct network has no known time source. The ESP now uses the configured 2.4 GHz home Wi-Fi briefly at startup to synchronize with NTP, applies Eastern daylight saving rules, disconnects, and reconnects to the scanner. `GATEWAY.TXT` reports whether the clock has a valid date. The home credentials are in the ignored `main/scanner_wifi_local.h` and firmware image, not in tracked source.

Firmware SHA-256 `6E90A4BB1910F5C0EC393EF00405CCC0AA83AB7C060913C1FCE4E3890F6D9FEE` flashed on COM3 with verified hashes. After normal RESET, `S:` returned with `Clock synchronized: yes`, scanner Wi-Fi connected, and `GATEWAY.TXT` modified at 9/18/2026 11:52:34 PM local time. The next scan produced `SCAN0011.JPG` (2,094,342 bytes, 9,058 ms capture) with a 9/18/2026 11:53:06 PM modified time. `GATEWAY.TXT` reported scan complete and scanner released. The JPG fully decoded at 2550 x 3264 RGB, 300 dpi, quality 75. Existing scans retain their original timestamps because their historical creation times cannot be reconstructed reliably from FAT metadata.

## Display settings match scan request (2026-09-18)

The display footer still contained a literal `600 DPI | RGB | JPG 50` after the scan request changed to 300 dpi and quality 75. The scan request, page-end limits, `GATEWAY.TXT`, and display footer now use constants in `scanner_settings.h`. Protocol and display-model tests passed. Firmware SHA-256 `B6F0D7C67DACE31ED5F7885492FE6C3C927E64B6FA8F05FB592AE3FBE1CEBBED` flashed on COM3 with verified hashes. After normal RESET, the user confirmed the display reads `300 DPI | RGB | JPG 75` at READY TO SCAN. `S:` returned with the correct settings and synchronized clock in `GATEWAY.TXT`.

## Remove the SD status file (2026-09-19)

`GATEWAY.TXT` was diagnostic output only; the firmware never read it. Its writer and boot/scan call sites were removed. After flashing the new firmware on COM3 and resetting, the existing file was deleted from `S:` in File Explorer. A new scan saved `SCAN0014.JPG` (847,210 bytes), fully decoded at 1680 x 2528 RGB, 300 dpi, quality 75, with a 9/19/2026 12:05:02 AM file timestamp. `GATEWAY.TXT` remained absent after the scan.

## Idle display and LED timeout (2026-09-19)

The ESP now turns off the display backlight and panel and clears the status LED after five minutes without a page or scanner-status change. It continues polling the feeder and serving the USB drive. A new page or status update wakes the panel and LED and redraws the current status. The timer is reset by displayed scan progress and status changes; it does not put the ESP or scanner into deep sleep.

The image flashed on COM3 with verified hashes. After a normal reset, the user confirmed `READY TO SCAN` and a green LED. Following an idle interval, the user confirmed both indicators were off while `S:` remained mounted. Inserting a page woke both indicators. The subsequent storage check found the FAT32 root directory empty apart from Windows' `System Volume Information`; read-only CHKDSK reported errors and about 63 MB in two recoverable files. The USB device later disconnected during a read-only disk image. No repair or further scan has been attempted. The completed JPEG for this wake test has not been verified, and the storage fault requires investigation before this firmware can be considered fully validated.

## Reliability candidate (2026-09-20)

The reliability branch changes normal USB access to read-only. Writable maintenance requires an awake, idle two-second BOOT hold and suspends scanning. Resuming automatic mode requires accepted host eject, successful status delivery in the same USB session, and another local hold. Reset, unplug and elapsed idle time cannot prove host release. Physical writes complete before USB success is reported, and SD errors remain visible to the storage controller. The exact Windows eject sequence and real failure behavior are still pending hardware acceptance.

The capture pipeline now publishes a validated, height-normalized, full-width `SCANnnnn.JPG` before attempting a separate optional `CROPnnnn.JPG`. Width detection remains a darkness heuristic, so the original preserves content that could be mistaken for scanner background. The validator checks every entropy row with bounded workspace. Exclusive scratch creation and no-replacement publication protect existing names; failed capture leaves the original scratch file, and derivative failure retains the saved original. Received bytes, published bytes, scanner release and crop outcome have separate meanings in the retained result.

Current activity and the last scan result are separate. A short awake, idle BOOT press acknowledges an alert without clearing storage uncertainty. Storage faults enter a responsive stopped or read-only recovery state. The display shows current clock validity before the first scan. Five-minute visual sleep tracks display and LED confirmation independently and bounds off retries. Following an uncertain LCD DMA completion, GPIO backlight-off is still attempted, but further panel commands and resource destruction are avoided to keep the main loop responsive.

Earlier quality-75 entries above describe historical scanner observations. Their eight reported luminance values and successful decode do not constitute a retained complete Epson quality-75 table fixture. The current verifier explicitly reports quality as unverified unless a complete registered table set is requested. Hardware acceptance must calibrate quality 75 with a known scanner sample, verify it positively, and reject a known quality-50 sample against it. Synthetic JPEG test quality settings are not scanner calibration evidence.

No firmware from this implementation session has been flashed, and the earlier damaged card has not been used for testing. The [acceptance record](qa/reliability-acceptance.md) records software evidence and the remaining physical gates. The cause of the earlier FAT incident remains unproved.
