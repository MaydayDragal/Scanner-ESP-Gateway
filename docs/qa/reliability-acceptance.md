# Reliability acceptance

## Status

**Reliability software gate passed; hardware acceptance in progress.** Implementation began on September 20, 2026, on `reliability/qa-2026-09-20`. The working tree is `.worktrees/reliability`; the original checkout and its user changes are preserved.

The target is the Waveshare ESP32-S3-LCD-1.47 (non-B), with the Epson ES-60W. Defaults remain 300 dpi RGB, scanner JPEG quality 75, and a 300-second visual idle timeout. No PSRAM or flash-partition expansion is included in this release.

The earlier damaged card is excluded from fault testing. The cause of its FAT error remains unproved. The candidate has now been flashed and booted on a user-confirmed expendable card. Initial read-only checks and an independent reader baseline are recorded below. Destructive fault tests have not yet run; the earlier damaged card was not used.

## Implementation checkpoints

| Work | Evidence | Remaining gate |
|---|---|---|
| Reproducible host tests and honest quality verification (R1) | `56e2893`; clean-input host run and fresh ESP-IDF build passed. R10 registered complete tables from a known quality-75 scanner sample; eight focused verifier cases pass, including table mismatch and unregistered quality rejection. | Hosted CI has not run. Controlled real quality-50 sample rejection against quality 75 remains pending. |
| Physical USB writes and error propagation (R2) | `7dccd69`; 16 tests use the actual controller, patched component, and pinned TinyUSB MSC core. Premature success and hidden write failure were reproduced before the correction. Independent review passed. | Real Windows/SD behavior. |
| Safe USB modes and eject (R3) | `3da2d2d`; 42 actual USB cases, four actual button-driver cases and 12 actual-main cases pass. Producer/button reviews, SDK build and combined host suite passed. | Windows safe eject and explicit resume. |
| Original preservation and streaming JPEG validation (R4/R5) | `446ed41` plus SDK filename-bound correction `63566d2`; 59 capture scenarios (including the R6 phase observer), 18 malformed JPEG variants, 300/600 dpi quality-50/75 synthetic corpus, maximum dimensions, allocation failures and width-crop tests pass. Fix reviews and ESP-IDF build passed. | Actual scanner/card corpus. |
| Retained outcomes and visual recovery (R6) | Producer `9156a72`, integration `aca8efb`; 22 actual gateway-main cases, 12 preserved R3 main cases, 11 visual-driver cases and seven pinned LED transport cases pass. Integration/fix review, final host suite and fresh ESP-IDF build passed. | Five-minute physical sleep/wake and stopped recovery. |
| Clock validity (R7) | Producer `3449036`, current/retained main integration `aca8efb`; three strict native model/Wi-Fi service modes, UI integration, reviews and final SDK build pass. | Home AP/DNS/NTP hardware behavior; no-local-header host branch was not exercised. |
| Length-bounded scanner status (R8) | `59017ee`; malformed payload regression and protocol suite pass; review passed. | No additional scoped software gate. |
| Legacy probe network-profile preservation (R9) | `ff32947`; seven mocked tests pass, including partial profile creation and native command parsing; review passed. No real adapter changes. | Native parser requires English Windows field labels. |

## Software evidence

- ESP-IDF 5.5.5, esp_tinyusb 2.3.0 local override, TinyUSB 0.21.0~2, led_strip 3.0.3.
- The local overrides preserve upstream licenses and hashes. Offline MSC tests check the exact pinned core; resolved-source drift fails before test compilation. The LED override retains all 32 original files and patches only its RMT implementation.
- A narrow pre-project lock helper rebases only the two reviewed local component paths. Seven host tests and an installed-manager two-stale-path check passed; other lock values remain unchanged. ESP-IDF configuration resolved both local overrides successfully.
- The R2-only fresh build passed: **957,648 bytes**, leaving **90,928 bytes** in the 1,048,576-byte application partition. This is an intermediate build, not the final candidate size.
- The corrected JPEG/clock snapshot built at **957,856 bytes**, and its R6 producer/LED overlay built at **958,800 bytes**. The corrected R3 storage producer overlay built at **959,936 bytes**, leaving **88,640 bytes**. These intermediate builds do not include final R6 main integration.
- R3 producer review corrections passed 42 actual USB cases. SDK compile-command inspection confirmed exactly one generated MSC core translation unit and an unchanged pinned upstream core hash. Corrected button/main integration passed review and built at **961,632 bytes**, leaving **86,944 bytes**.
- The combined host suite passed before the R4/R5 final height fix and R6 integration. Its log is the local `build/reliability-host-integration.log`; final integrated results are recorded below.
- A later combined host run including corrected R3 and the R6/R7 producers passed: `build/r3-integrated-host.log`. That intermediate run preceded final R6 main integration.
- The streaming JPEG workspace measured **7,592 bytes** against a compile-time 32 KiB ceiling. This excludes file, task, filesystem and other application memory; it is not total runtime heap usage.
- The final aggregate run passed on the source snapshot: `build/reliability-reviewed-host.log`, exit 0. It includes 42 USB lifecycle cases, 22 gateway-main cases, 12 preserved R3 main cases, 59 capture scenarios, 11 visual cases, seven LED transport cases, image/protocol/model tests, six verifier cases, seven lock-helper tests and seven mocked probe cases. Five PowerShell scripts also parsed successfully.
- A new build directory using tracked defaults and dummy scanner/time settings passed with ESP-IDF 5.5.5: `build/reliability-reviewed-idf-build.log`. The image is **963,040 bytes**, leaving **85,536 bytes** in the unchanged 1 MiB application partition. SHA-256: `0cacd6765bbe54ff1be42b3674c6ff8ee7cf2b1a471273ef2246ec3d90dc8415`. Exactly one generated patched MSC core unit is compiled. Production/test/helper hashes matched the frozen source manifest.
- This fresh build ran on Windows using the same SDK, defaults and build arguments as CI. The hosted Windows/Ubuntu jobs have not run. The `eim` wrapper can return zero after a failed child build, so completion was checked using the final build marker and fresh artifacts as well as its exit status.
- Final integration review found a repeated visual error hidden by sticky history after a successful wake. The corrected render APIs return current errors; same-code LCD and later-different-code LED regressions pass, and scoped re-review passed. Pending LCD DMA remains deliberately unrecoverable until restart, with GPIO backlight-off still reachable.
- Software-gate builds use dummy Wi-Fi values. A separate private candidate uses the existing ignored user configuration; no firmware binary is published. Diagnostics retain 64 bounded RAM records and do not survive cold power loss.

## Final candidate

| Field | Result |
|---|---|
| Source commit / working tree | Firmware source `aca8efb`; source/test changes committed locally. Final acceptance documentation is a separate commit. Earlier generated EXE/PDB files remain untracked after automatic approval review blocked cleanup; none is staged or part of the snapshot. |
| Full host suite | Passed; `build/reliability-reviewed-host.log`. |
| Fresh CI-equivalent ESP-IDF build | Passed locally with dummy settings; new `build/reliability-reviewed` directory, ESP-IDF 5.5.5, tracked defaults. Hosted CI not run. |
| Application size and partition headroom | Private candidate: **963,024 bytes**, **85,552 bytes free** (about 8.2%). Dummy software image: 963,040 bytes. |
| Firmware SHA-256 | `77e5126bfbf56f0c47a0a37d9684a9cad280901f1e0e520312d01bd0f811eb64` (private candidate; flashed and verified). |
| Build configuration | ESP32-S3, 16 MB flash, existing 1 MiB app partition, no PSRAM, 6,144-byte main-task stack, FreeRTOS 100 Hz; 300 dpi RGB / JPEG 75 / 300-second visual timeout. Private SSIDs/passwords omitted. |
| Source review of ownership, publication and errors | Task reviews and final R6 integration/fix review passed. No unresolved scoped software finding; hardware gates remain open. |
| Physical board revision and diagnostic path | Pending confirmation |
| Expendable test card identity and required backup | User confirmed the installed spare may be erased. Capacity 4,026,531,840 bytes, FAT32, partition offset 16,384; ESP serial SCANNER001 identifies the gateway, not the SD CID. Same card verified through Realtek PCIE reader at D: by matching two original JPG hashes. Those two JPGs were privately backed up. |

The private candidate build passed in the ignored source snapshot at `build/reliability-final-source/build/reliability-reviewed`, using the existing ignored user Wi-Fi configuration. Its log is `build/reliability-private-candidate-fix-build.log`; metadata is `build/reliability-private-candidate.json`. The Wi-Fi translation unit was confirmed recompiled and all dummy Wi-Fi sentinels were absent from the image. The earlier dummy image is retained separately as `build/reliability-reviewed-dummy.bin`. No configuration contents or binaries were published. The three bootloader/application/partition flash hashes verified on COM3. The user then reset the board and observed READY TO SCAN with a green LED.

## Initial hardware evidence

- Candidate flash: application SHA-256 above; all three flashed images verified. ROM identified ESP32-S3 revision v0.2. Embedded PSRAM was reported by ROM but remains disabled in this build.
- Startup: user observed READY TO SCAN and green LED. Windows identified the same approximately 4 GB ESP disk on S: with `IsReadOnly=True`.
- Baseline through ESP: read-only CHKDSK passed without filesystem errors. Windows reported a dirty flag; its origin was not established by this test.
- Independent reader: user moved the expendable card to D: on the Realtek PCIE reader with the ESP unplugged. Both original JPEG sizes/hashes matched. Read-only CHKDSK passed. After verified private JPG backups, `chkdsk D: /F /X` found no problems and `fsutil dirty query D:` reported NOT Dirty.
- A generated 262,144-byte `QAKEEP.BIN` was written/flushed/read back through the reader; SHA-256 `84847eecb599385241122ad099ce2c1e72adca12bba372569ae4124cffb0d804`. Both original JPG hashes stayed unchanged. After card return to ESP, all three hashes matched, S: was read-only and the dirty flag was clear. A Windows create attempt returned `0x80070013` (media write protected), without creating the file.
- Writable maintenance: Windows reported writable media. Exclusive create, flush, readback, rename and delete passed; `QAHOST.BIN` remains as a second preservation fixture (262,144 bytes, SHA-256 `5586591ae7045bf324d4c35dd5a8c6f2624b8e2a3b710ab6d6dec64de956e2ba`). The previous three hashes stayed unchanged. With paper loaded, a BOOT hold before Windows eject remained at MAINTENANCE / EJECT DRIVE ON PC and did not scan.
- Functional eject/resume: user-initiated Windows eject changed the display to HOLD BOOT TO RESUME; the still-enumerated ESP disk reported No Media. A fresh BOOT hold restored read-only automatic mode. On the second cycle the queued page scanned automatically. These observations do not prove USB status-packet timing or internal mount ordering.
- First corpus page: `SCAN0003.JPG` (1,328,103 bytes, 2550 x 2544) and `CROP0003.JPG` (1,268,900 bytes, 1680 x 2544) fully decoded as RGB, 300 dpi, quality 75. Their current UTC modification times were 2026-09-20 15:24:42 and 15:25:14. The crop removed the broad right-side dark area; a thin paper edge remains. Decoded interior pixels match the original, excluding the final 16 pixels for JPEG boundary effects. All four older file hashes remained unchanged and S: reported NOT Dirty. Observed scan-related USB absence was 52.657 seconds, including validation, crop and storage restoration; it is not a pure capture benchmark.
- Second corpus page: full-width `SCAN0004.JPG` (2,131,512 bytes, 2550 x 3312) fully decoded as RGB, 300 dpi, quality 75; no width derivative was produced. Timestamp was 2026-09-20 15:34:32 UTC; SHA-256 `8a51a955bba7e44e1a296a155b07511abbf290ef243f2f406e7114bc9aa306e7`. All six previous file hashes matched. S: returned read-only and NOT Dirty. The observer measured about 29 seconds of USB absence; polling includes filesystem-call latency, so this is approximate.
- Quality-75 calibration: both complete 64-coefficient tables were retained from that original, with source commit, firmware hash, dimensions, timestamp and sample hash. The original SHA-256 is `5e23b082cb6bd0a69c42ef2d7536535824a3e7de5081b9adb93793249f226838`; derivative SHA-256 is `d1e6780c9c22c440255da06c7c1429f3d59700a9413fc695e7f01f3a6d93abe4`. Both pass explicit quality-75 verification and reject quality 50 in normal and optimized Python. Eight focused verifier tests and an independent fixture/provenance review passed. A controlled real quality-50 sample must still reject quality 75 before this hardware row closes.
- Raw local evidence is in ignored `build/r10-hardware/`; private JPG backups are excluded from Git. The MSC-only device exposes no diagnostic-ring export or usable application serial console. Display release state can support functional eject/resume testing; direct USB status timing and APP-mount ordering require additional instrumentation and remain unproved physically.

## Hardware acceptance matrix

Record the candidate hash, card identity, exact operations, file hashes, timestamps, USB sequence and observed results for each row. Use only the identified expendable card for writes, injected faults and power interruption. No automatic formatting or repair is part of firmware startup.

| Check | Required result | Actual result |
|---|---|---|
| Automatic mode | USB advertises and enforces read-only access; copying files works. Insert a page during a host read and record the interrupted read/USB sequence; a page acquires the card only after USB is quiescent. Previously saved file hashes remain unchanged. | Read-only advertisement, blocked Windows creation, copies and preservation passed. Host-read/page overlap and physical ownership timing pending. |
| Writable maintenance | Awake BOOT hold selects maintenance, re-enumerates writable, and prevents page capture. Create/read/rename/delete operations and preserved-file hashes agree. | Passed observed entry, writable file operations, preservation and loaded-page blocking. |
| Windows safe eject and resume | Observe accepted eject and successful status transfer in the same session; local resume switches to automatic. No APP mount occurs earlier. | Functional eject/LCD release/local resume passed twice; queued page scanned. Direct status-transfer and APP-mount timing remain unproved physically. |
| Invalid release sequences | Prevented/rejected eject, failed or aborted status, stale completion, reset and unplug cannot authorize APP access. Unsupported host sequences stay blocked with an instruction. | Resume-before-eject stayed blocked with loaded paper. Other physical cases pending. |
| Mixed scan corpus | 20 successive narrow/full-width pages, dark edge content, photos, borders and skew without reset; originals preserved, derivatives valid, sizes and numbering correct. Check through a stable reader and require no FAT errors after the corpus. | 2/20 passed: narrow original/crop and full-width original with no unnecessary width crop. Independent-reader check after the completed corpus pending. |
| Profiles | Controlled 600 dpi and quality-50 builds produce valid originals/derivatives; restore 300 dpi/75 afterward. | Pending |
| JPEG quality calibration | Extract complete quantization tables from a known scanner quality-75 sample and record provenance without committing the private JPEG. Register those tables, require positive verification of that known sample with `--quality 75`, then require a known quality-50 sample to fail `--quality 75` with a table mismatch. Unregistered-quality rejection alone does not complete this check. | Known quality-75 original and derivative registered and verified. Real quality-50-as-75 mismatch check pending controlled profile capture. |
| Clock | Successful boot synchronization gives current FAT dates; missing home AP, DNS or NTP yields a retained clock warning and bounded boot behavior. | Normal settings footer and current timestamps on first scan observed. Failure cases pending. |
| Full card / physical I/O error | File outcomes and USB status report failures; existing file hashes remain unchanged; no unsafe recovery or false saved result. Check FAT consistency through a stable reader after every fault, including full-card and injected I/O cases. | Pending |
| Interrupted publication | Controlled interruption during original and derivative stages retains existing data; no invalid final file is treated as complete. Check FAT consistency after each fault through a stable reader. | Pending |
| Idle and wake | Both indicators turn off after five minutes; button/page wakes them; a gesture starting asleep cannot also change storage mode; the next scan completes. | Pending |
| Missing/unmountable card | Responsive stopped or read-only recovery UI; no automatic format, repeated capture or unproved APP ownership. | Pending |
| Retained result and headless operation | Empty-feeder polling does not erase a failure; cleanup/crop warnings survive; visual-driver faults are retained and scanning remains responsive where storage is safe. | Pending |

## Release decision

Pending. Performance and feature releases remain gated on closure of the P1 findings and successful storage/image hardware acceptance. A host test, successful JPEG decode, or earlier firmware's hardware result does not substitute for this candidate's physical checks.
