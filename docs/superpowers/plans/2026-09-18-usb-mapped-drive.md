# USB mapped drive implementation plan

Goal: Continuous Windows drive letter while scanning, with measurable transfer improvements.
Architecture: USB NCM Ethernet (192.168.77.1 device, .2 host), read-only WebDAV, ESP-owned FATFS. No forwarding or default gateway on the USB link. Publish only closed SCANnnnn.JPG files; unfinished TMP files remain private. Windows WebClient maps S:. Capture at 300 dpi; JPEG quality was later changed from 100 to 50.

- [x] Add NCM network interface and permanent SD mount; retain automatic scan loop.
- [x] Add bounded read-only WebDAV GET/HEAD/OPTIONS/PROPFIND with strict filename validation and range support; reject writes and traversal. Restrict access to USB interface.
- [x] Increase network windows and buffered writes; retain final fsync before publication and report progress every MiB without interrupting the scanner stream. Record capture duration.
- [x] Build and run protocol/security tests; review implementation.
- [x] Flash, configure USB IPv4 and WebClient, map drive, verify files and rejected writes.
- [x] Verify copying a previous image during a new scan without USB removal; decode new image and compare transferred hashes.

Validation must separate firmware build success from actual Windows WebDAV compatibility. Preserve all existing scans and current firmware backup. Hardware USB remains full speed, so no promise of high-speed USB rates.

2026-09-18 verification: ESP-IDF build and host protocol/path tests pass. The flashed firmware passes the read-only WebDAV smoke test, including 16 sequentially held sessions. Windows WebClient maps S: and lists completed scans. During a live scan, S: copied the prior 12,670,837-byte SCAN0014.JPG in 15.88 seconds; its SHA-256 matched the source. The newly captured SCAN0015.JPG copied through S: and decoded fully at 2550 x 4200 RGB, 300 dpi, with quality-100 tables.
