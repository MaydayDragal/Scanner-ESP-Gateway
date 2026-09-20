# Reliability acceptance

## Status

**Reliability software gate passed; hardware acceptance pending.** Implementation began on September 20, 2026, on `reliability/qa-2026-09-20`. The working tree is `.worktrees/reliability`; the original checkout and its user changes are preserved.

The target is the Waveshare ESP32-S3-LCD-1.47 (non-B), with the Epson ES-60W. Defaults remain 300 dpi RGB, scanner JPEG quality 75, and a 300-second visual idle timeout. No PSRAM or flash-partition expansion is included in this release.

The earlier damaged card is excluded from fault testing. The cause of its FAT error remains unproved. No card access, real network changes, flashing, or physical fault testing has been performed during this implementation session.

## Implementation checkpoints

| Work | Evidence | Remaining gate |
|---|---|---|
| Reproducible host tests and honest quality verification (R1) | `56e2893`; clean-input host run and fresh ESP-IDF build passed; six verifier cases pass, including explicit registered-table mismatch and unregistered quality rejection. | Hosted CI has not run. Epson quality-75 tables require a known scanner sample. |
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
| Firmware SHA-256 | `77e5126bfbf56f0c47a0a37d9684a9cad280901f1e0e520312d01bd0f811eb64` (private candidate; not flashed). |
| Build configuration | ESP32-S3, 16 MB flash, existing 1 MiB app partition, no PSRAM, 6,144-byte main-task stack, FreeRTOS 100 Hz; 300 dpi RGB / JPEG 75 / 300-second visual timeout. Private SSIDs/passwords omitted. |
| Source review of ownership, publication and errors | Task reviews and final R6 integration/fix review passed. No unresolved scoped software finding; hardware gates remain open. |
| Physical board revision and diagnostic path | Pending confirmation |
| Expendable test card identity and required backup | Pending confirmation and stable-reader preparation |

The private candidate build passed in the ignored source snapshot at `build/reliability-final-source/build/reliability-reviewed`, using the existing ignored user Wi-Fi configuration. Its log is `build/reliability-private-candidate-fix-build.log`; metadata is `build/reliability-private-candidate.json`. The Wi-Fi translation unit was confirmed recompiled and all dummy Wi-Fi sentinels were absent from the image. The earlier dummy image is retained separately as `build/reliability-reviewed-dummy.bin`. No configuration contents or binaries were published. Flash this recorded candidate only after the expendable-card and physical setup checks are complete.

## Hardware acceptance matrix

Record the candidate hash, card identity, exact operations, file hashes, timestamps, USB sequence and observed results for each row. Use only the identified expendable card for writes, injected faults and power interruption. No automatic formatting or repair is part of firmware startup.

| Check | Required result | Actual result |
|---|---|---|
| Automatic mode | USB advertises and enforces read-only access; copying files works. Insert a page during a host read and record the interrupted read/USB sequence; a page acquires the card only after USB is quiescent. Previously saved file hashes remain unchanged. | Pending |
| Writable maintenance | Awake BOOT hold selects maintenance, re-enumerates writable, and prevents page capture. Create/read/rename/delete operations and preserved-file hashes agree. | Pending |
| Windows safe eject and resume | Observe accepted eject and successful status transfer in the same session; local resume switches to automatic. No APP mount occurs earlier. | Pending |
| Invalid release sequences | Prevented/rejected eject, failed or aborted status, stale completion, reset and unplug cannot authorize APP access. Unsupported host sequences stay blocked with an instruction. | Pending |
| Mixed scan corpus | 20 successive narrow/full-width pages, dark edge content, photos, borders and skew without reset; originals preserved, derivatives valid, sizes and numbering correct. Check through a stable reader and require no FAT errors after the corpus. | Pending |
| Profiles | Controlled 600 dpi and quality-50 builds produce valid originals/derivatives; restore 300 dpi/75 afterward. | Pending |
| JPEG quality calibration | Extract complete quantization tables from a known scanner quality-75 sample and record provenance without committing the private JPEG. Register those tables, require positive verification of that known sample with `--quality 75`, then require a known quality-50 sample to fail `--quality 75` with a table mismatch. Unregistered-quality rejection alone does not complete this check. | Pending |
| Clock | Successful boot synchronization gives current FAT dates; missing home AP, DNS or NTP yields a retained clock warning and bounded boot behavior. | Pending |
| Full card / physical I/O error | File outcomes and USB status report failures; existing file hashes remain unchanged; no unsafe recovery or false saved result. Check FAT consistency through a stable reader after every fault, including full-card and injected I/O cases. | Pending |
| Interrupted publication | Controlled interruption during original and derivative stages retains existing data; no invalid final file is treated as complete. Check FAT consistency after each fault through a stable reader. | Pending |
| Idle and wake | Both indicators turn off after five minutes; button/page wakes them; a gesture starting asleep cannot also change storage mode; the next scan completes. | Pending |
| Missing/unmountable card | Responsive stopped or read-only recovery UI; no automatic format, repeated capture or unproved APP ownership. | Pending |
| Retained result and headless operation | Empty-feeder polling does not erase a failure; cleanup/crop warnings survive; visual-driver faults are retained and scanning remains responsive where storage is safe. | Pending |

## Release decision

Pending. Performance and feature releases remain gated on closure of the P1 findings and successful storage/image hardware acceptance. A host test, successful JPEG decode, or earlier firmware's hardware result does not substitute for this candidate's physical checks.
