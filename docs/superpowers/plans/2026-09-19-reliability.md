# Reliability release implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an independently usable gateway release that protects storage and original scan content and exposes failures accurately.

**Architecture:** Patch the pinned MSC component's write contract, separate storage mode from ownership, publish originals before derivatives, and retain last-result state independently of scanner readiness. Keep control and filesystem actions in the main task.

**Tech Stack:** ESP-IDF 5.5.5, TinyUSB 0.21.0~2, esp_tinyusb 2.3.0 override, C/FatFs, Python/ziglang/Pillow, PowerShell.

**Spec:** [Design brief](../specs/2026-09-19-qa-improvements-design.md). [Master plan](2026-09-19-qa-improvements.md).

**Execution:** Began September 20, 2026, on `reliability/qa-2026-09-20`. See the [acceptance record](../../qa/reliability-acceptance.md) for current source/test evidence, deviations and remaining physical gates. Quality-75 calibration is deliberately deferred to a known scanner sample at R10.

## Global constraints

- Target the existing Waveshare ESP32-S3-LCD-1.47 and Epson ES-60W; verify the physical board revision before enabling new pins or PSRAM.
- Use ESP-IDF 5.5.5 and TinyUSB 0.21.0~2; maintain a checked-in, narrowly patched esp_tinyusb 2.3.0 override.
- Default scanning remains 300 dpi RGB, JPEG quality 75, with a 300-second display/LED timeout.
- Only one owner may access the SD filesystem; never format automatically or overwrite an existing scan or working file.
- Keep credentials and private firmware binaries out of Git, CI artifacts, logs, and published reports.
- Do not recreate GATEWAY.TXT or require a companion PC application.
- Use expendable media for write-fault and power-loss testing; the previously damaged card is excluded.

## Review focus

- Already-received USB writes around a detach boundary: R2.
- Rejected, prevented, incomplete, or stale eject events: R3.
- A dark printed page that resembles scanner background: R4/R5.
- Filename collision, disk-full, or interruption during derivative publication: R4.
- Empty feeder after a failed scan, missing card at boot, and invalid time: R6/R7.

## File boundaries and test conventions

Existing protocol/display/storage tests remain the entry point. New C tests are compiled with the existing `ziglang cc -std=c11 -Wall -Wextra -Werror` pattern and added to `tests/run_protocol_tests.ps1`. Tests for SDK boundaries use explicit stubs, never the live SD card.

| Unit | Responsibility |
|---|---|
| `components/esp_tinyusb/` | Reviewed upstream 2.3.0 component plus narrow synchronous-write, eject-event, and mount-error patches. |
| `main/usb_storage.c/.h`, `storage_handoff_model.c/.h`, new `storage_mode_model.c/.h` | Own transport/filesystem transitions, user mode, release-generation evidence, recovery. |
| New `main/scan_files.c/.h` | Reserve names, track owned scratch files, publish without replacement, actual sizes. |
| New `main/jpeg_stream.c/.h` | Bounded baseline-JPEG inspection and row validation; no publication decisions. |
| `scanner_capture.c/.h`, `jpeg_crop.c/.h`, `jpeg_width_crop.c/.h` | Capture transaction, metadata normalization, optional derivative. |
| New `main/gateway_state_model.c/.h`, `gateway_diagnostics.c/.h` | Current phase, retained outcome, bounded event history. |
| New `main/scanner_clock_model.c/.h` | Clock validity/source/error, consumed by UI and capture outcome. |
| New `main/scanner_button_model.c/.h`, `scanner_button.c/.h` | Input-only GPIO0 sampling and bounded events; no direct storage/UI actions. |

Each implementation task ends with a focused commit containing source, tests, and relevant documentation. Add new source/dependencies to `main/CMakeLists.txt` in the task that creates them.

## R1 — Reproducible baseline and honest test results

**Files:** modify `tests/test_jpeg_width_crop.py`, `tests/verify_scan.py`, `tests/test_verify_scan.py`, `tests/run_protocol_tests.ps1`, `README.md`; create `tests/requirements.txt`, `tests/fixtures/epson_quantization.json`, `.github/workflows/qa.yml`.

**Interfaces:** `verify_scan.py` accepts an optional `--quality`. Explicit quality verification requires a registered, verified quantization-table set; absent evidence is a nonzero `quality unverified` result. Omitted quality performs decode/dimension inspection and labels quality unverified.

- [x] Preserve the existing dirty baseline, audit evidence, and idle-timeout work. Record current passing commands and locked dependency versions; use an isolated execution branch without losing those changes.
- [x] Add negative quality tests and run them red. Store only table arrays/provenance, never private scan images. Example assertions:

```python
# Extend the existing unittest fixture/subprocess pattern.
self.assertNotEqual(verify(quality50_fixture, expected_quality=75).returncode, 0)
self.assertEqual(verify(quality50_fixture, expected_quality=50).returncode, 0)
self.assertNotEqual(verify(bad_dimensions, python_optimized=True).returncode, 0)
```

Define the test-local `verify(path, expected_quality=None, python_optimized=False)` wrapper to run `tests/verify_scan.py` with `subprocess.run(..., capture_output=True)`. Use explicit exceptions/exits in the verifier; validation cannot depend on Python assertions.
- [x] Compile the width-test DLL in `TemporaryDirectory`, pin the working Python test package versions, and prove the full host runner works with no pre-existing `build` directory in a temporary copy of tracked inputs.
- [x] Add CI jobs for host tests and a fresh IDF build with dummy scanner/time credentials created only inside the job. Do not upload firmware artifacts containing real credentials. Keep the normal command stable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1
eim run 'idf.py -B build/reliability-clean -D SDKCONFIG=build/reliability-clean/sdkconfig build' v5.5.5
```

- [ ] Verify quality-75 provenance from a known scan or, at R10, an explicit scanner-setting sample. Until then the verifier must say unverified. Commit `test: make gateway QA reproducible and quality checks explicit`.

## R2 — Correct MSC write completion and error propagation

**Depends on:** R1. **Files:** create `components/esp_tinyusb/` from resolved 2.3.0 with license and `PATCHES.md`; modify its `tinyusb_msc.c`, `main/idf_component.yml`, `dependencies.lock`, `main/usb_storage.c/.h`, `tests/run_usb_storage_tests.py`, `tests/test_usb_storage.c`, and SDK stubs.

**Contract:** `tud_msc_write10_cb` returns accepted bytes only after the physical write succeeds. A failed physical write returns `TUD_MSC_RET_ERROR` and records a bounded local error. The host may receive generic sense from the pinned TinyUSB core; failed command status is mandatory.

- [x] Make dependency resolution explicit with the following mapping in `main/idf_component.yml`; the override path is relative to that manifest. Keep TinyUSB exactly `0.21.0~2`. Verify the clean-build resolved source path points to the checked-in component. Document upstream hashes and every patch.

```yaml
dependencies:
  espressif/esp_tinyusb:
    version: "==2.3.0"
    override_path: "../components/esp_tinyusb"
  espressif/tinyusb: "==0.21.0~2"
# Preserve the manifest's other existing dependencies.
```
- [x] Port the existing QA FIFO reproduction into production-controller regression cases. Compile the changed production callback as well as the actual controller. The intended assertions are:

```c
/* In the extended lifecycle harness, after completion-before-detach. */
assert(accepted_bytes == 512);
assert(physical_writes == 1);
assert(pending_dependency_writes == 0);
/* With physical failure injected. */
assert(write_result == TUD_MSC_RET_ERROR);
assert(local_write_error != ESP_OK);
```

Run `python tests/run_usb_storage_tests.py`; verify new cases fail on the baseline.
- [x] Replace the dependency's copied-buffer/deferred-write path with its existing synchronous sector-write function:

```c
esp_err_t err = msc_storage_write_sector(lun, lba, offset, bufsize, buffer);
if (err != ESP_OK) {
    /* Publish a bounded project I/O-error event; do not render or mount here. */
    return TUD_MSC_RET_ERROR;
}
return (int32_t)bufsize;
```

Remove unused deferred-write state and update related lifetime checks/tests. Keep buffer size unchanged for this release. Extend the component event API with an I/O-error event carrying LUN, operation, and `esp_err_t`; update `storage_event()` to switch explicitly on event type, so error/eject events cannot satisfy a mount wait.
- [x] Test single/multiple sectors, physical timeout, error reporting through actual MSC command processing, detach while a callback is executing, late acknowledgements, and recovery. Preserve refusal to force-delete a task whose safe boundary has not been acknowledged.
- [x] Run storage tests, full host tests, and a fresh build. Commit `fix: complete MSC writes before acknowledgement and detach`.

## R3 — Safe USB modes, explicit eject, and recoverable startup

**Depends on:** R2. **Files:** modify storage controller/model, `main.c`, component MSC mount/eject callbacks; create `storage_mode_model.c/.h`, button model/driver, `tests/test_storage_mode_model.c`, `tests/test_scanner_button_model.c`; extend lifecycle/main stubs.

**Proposed public interfaces:** existing acquire/expose APIs remain, but enforce mode rules. Add:

```c
typedef enum { STORAGE_AUTO_RO, STORAGE_MAINTENANCE_RW,
               STORAGE_RECOVERY_RO } storage_mode_t;
storage_mode_t usb_storage_mode(void);
esp_err_t usb_storage_enter_maintenance(void);
bool usb_storage_host_released(void);
esp_err_t usb_storage_resume_automatic(void);
/* No API grants APP access from an uncertain ownership state. */
```

- [x] Add red tests for writes in automatic mode, paper while in maintenance, read-only recovery, and resume before release. Add accepted/rejected/prevented eject, status-completion failure, repeated eject, USB reset, and stale-session events.
- [x] Default to `STORAGE_AUTO_RO`; advertise write protection consistently and reject writes. Enter maintenance only from healthy idle; re-enumerate to publish changed protection. Suppress page triggering while writable.
- [x] Add an accepted-eject latch in the pinned component's `tud_msc_start_stop_cb`; require `load_eject && !start` and removal permitted. Complete release only after matching `tud_msc_scsi_complete_cb` in the same USB-session generation. That callback also occurs after failed commands, so CDB equality alone is insufficient. Clear latches on reset/reconnect/error; reject new reads/writes after accepted eject. Handle the dedicated PREVENT/ALLOW callback.
- [x] Detect every USB bus reset through the pinned stack's `tud_event_hook_cb()`; ATTACHED/DETACHED or `tud_umount_cb()` alone are insufficient. Invalidate session evidence with ISR-safe generation handling, without blocking, rendering, or mounting in the hook. Test a bus reset between accepted eject and completion.
- [x] Handle and test `SYNCHRONIZE CACHE(10)` in maintenance mode as a barrier for completed physical I/O. It cannot authorize APP access, substitute for eject, or conceal a latched write failure. The actual-core host tests pass; verify this command alongside the real Windows eject sequence at R10 (pending).
- [x] Add minimal runtime BOOT input: GPIO0 input only, 30 ms debounce, require release after boot, and enqueue events. A gesture starting asleep wakes and consumes the entire gesture; it cannot also select maintenance/resume. When already awake, a two-second hold while idle selects maintenance; a hold after completed host eject requests automatic mode. Before eject, show `EJECT DRIVE ON PC`. A press must never force ownership. Later F2 replaces these minimal controls with a menu and retains wake-only gesture consumption.
- [x] Initialize the medium/MSC storage object in `MOUNT_USB` with transport stopped; attempt APP mount separately. Patch mount setters to return errors and not falsely overwrite ownership. On filesystem mount failure, clean residual registration with USB stopped and expose only read-only recovery, if the medium itself works. Missing card/unsafe transport remains stopped. Keep automatic formatting disabled.
- [x] Pin invariants in tests and run `python tests/run_usb_storage_tests.py` plus the full runner:

```c
assert(!automatic_write_allowed);
assert(!app_access_after_rejected_or_stale_eject);
assert(!capture_started_while_maintenance);
assert(!format_called_on_mount_failure);
```

Commit `feat: separate automatic scan and writable maintenance modes`. At R10 verify which Windows eject sequence actually completes; unsupported sequences remain safely blocked with instructions, not inferred from a disconnect.

## R4 — Transactional file ownership and original-first publication

**Depends on:** R1; integrate with R3 before hardware. **Files:** create `scan_files.c/.h`, `tests/test_scanner_capture.c`, `tests/run_capture_tests.py`, filesystem/network stubs; modify `scanner_capture.c/.h` and crop target I/O.

**Implementation unit:** R4 and R5 form one implementation/review unit with a shared passing-test/build gate and final commit. Implement reservations/publication primitives first, then R5's validator/writer, then connect capture and run the complete failure matrix. R4 does not require a completed R5 before its primitives can be developed, and is not independently released.

**Interfaces/data:** add these to `scanner_capture_result_t`; `scan.bytes` stays the received count and `scan.released` stays protocol cleanup status.

```c
typedef enum { SCANNER_CROP_NOT_REQUESTED, SCANNER_CROP_NOT_NEEDED,
               SCANNER_CROP_SAVED, SCANNER_CROP_FAILED } scanner_crop_outcome_t;
/* Fields added to the existing capture result. */
bool file_saved;
uint32_t saved_bytes;
char crop_filename[13];
uint32_t crop_bytes;
scanner_crop_outcome_t crop_outcome;
```

`scan_files.c` owns reservations and tracks which scratch files this attempt created. Capture orchestrates the transaction; JPEG writers accept already-open `FILE *` targets and cannot unlink paths.

- [x] Add collision/failure tests. A number is available only when `SCANnnnn.JPG`, `SCANnnnn.TMP`, legacy `SCANnnnn.CRP`, `CROPnnnn.JPG`, and `CROPnnnn.TMP` are absent. Treat lookup errors other than not-found as errors. Run `python tests/run_capture_tests.py` red.
- [x] Exclusively create scratch files; never create empty final JPG placeholders. Implement publication through a no-replacement adapter verified against the installed FatFs behavior. All calls require APP ownership. Only current-attempt scratch files may be removed; preserve partial originals for inspection.
- [x] Publish in this order, using the validator contract in R5:

```text
receive SCANnnnn.TMP -> flush/sync/close -> validate every row
-> normalize height metadata -> sync/close -> publish SCANnnnn.JPG
-> optional CROPnnnn.TMP -> write/sync/close -> publish CROPnnnn.JPG
```

- [x] Inject open/connect/short-write/flush/fsync/close/rename failures and a stop at every arrow. Original publication must precede derivative work. If derivative creation fails, set `file_saved=true`, `crop_outcome=SCANNER_CROP_FAILED`; keep the original. Obtain saved sizes from the published files rather than received bytes.
- [x] Assert no-crop never touches an existing `.CRP`, final collisions never overwrite, and derivative failures never delete the original. Complete R5 before running the joint capture/image/full-test and build gate; include these changes in the shared R4/R5 commit.

## R5 — Validate all JPEG rows and make cropping recoverable

**Depends on:** R1 and the R4 file primitives, within the shared R4/R5 implementation unit. **Files:** create `jpeg_stream.c/.h`, `tests/test_jpeg_pipeline.py`; modify both crop modules, capture integration, and existing JPEG tests.

**Proposed interfaces:** define `jpeg_status_t` with `OK`, `INVALID`, `IO_ERROR`, and `NO_MEMORY`; define `jpeg_expectations_t` with `JPEG_SCANNER_INPUT`/`JPEG_PUBLISHED` mode and canvas/page dimensions. Define `jpeg_info_t` with width, declared height, validated height, MCU/row counts, byte length, and header offsets. Define `jpeg_crop_proposal_t` with proposed width and confidence statistics. Then expose:

```c
jpeg_status_t jpeg_inspect(FILE *source, const jpeg_expectations_t *expected,
                           jpeg_info_t *info, jpeg_crop_proposal_t *proposal);
jpeg_status_t jpeg_normalize_height(FILE *source, const jpeg_info_t *info);
jpeg_status_t jpeg_write_width_crop(FILE *source, FILE *target,
                                    const jpeg_info_t *info, uint16_t width);
```

- [x] Convert the synthetic QA fixtures into correct-behavior tests: the dark-page item remains in the original; empty unsampled row is rejected; no-crop cannot delete another file. Add malformed tables, category/run overflow, stuffing/padding errors, truncated amplitudes, extra entropy, bad restart order, and trailing data. Run `python tests/test_jpeg_pipeline.py` red.
- [x] Implement bounded streaming inspection: 4 KiB reader, bounded tables, exact expected MCU count in every row, strict terminal EOI, valid all-one pad bits, and no trailing entropy/data. Parse header segments incrementally, keeping offsets instead of a fixed 64 KiB header. Avoid allocation sized to an entire compressed row. Test an explicit JPEG workspace ceiling of 32 KiB, excluding separately budgeted filesystem/task resources.
- [x] In `JPEG_SCANNER_INPUT` mode, permit only the documented oversized scanner height declaration, deriving the permitted entropy-row count and validated height from the checked page-end value and existing justified tolerance. In `JPEG_PUBLISHED` mode, require exact final frame dimensions and the corresponding encoded MCU count. Supply the derivative's actual width for its reinspection. Test scanner input, normalized original, and narrower derivative separately; reject wrong dimensions in each mode. Do not reject normal scanner input merely because its initial height metadata needs correction.
- [x] Collect optional crop statistics during inspection; a null proposal disables statistics, never validation. Retain the justified page-end tolerance separately from bitstream checks. Normalize height only after the complete source validates, preserving encoded rows and coefficients.
- [x] Default to an optional crop derivative with original retention. A no-crop result produces only the original. Keep the existing narrow-width guard until F6 and preserve one MCU margin; do not describe darkness detection as proof of a physical paper edge. Reinspect derived output before publication.
- [x] Run the 300/600 dpi, 50/75 quality corpus, maximum dimensions, allocation-failure tests, retained-pixel comparisons, and R4's complete transaction failure matrix. Pillow decode is a compatibility check in addition to strict validation. Run capture/image/full tests and build, then commit R4/R5 together as `fix: validate JPEGs and preserve originals with safe file transactions`.

## R6 — Persistent outcomes, stopped recovery, and reliable visual sleep

**Depends on:** R3/R4/R5. **Files:** create gateway state/diagnostic models, `tests/test_gateway_main.c`, `tests/test_scanner_visual.c`; modify `main.c`, display/LED models/drivers, idle model, and capture-result mapping.

**State contract:** current phase and last result are separate. Phases include STARTING, READY, ACQUIRING, CAPTURING, FINALIZING, RESTORING, MAINTENANCE, TIME_SYNC, and STOPPED. Last result stores saved/failed state, original/derivative names and sizes, cleanup/crop warnings, failed stage, error code, clock validity, and acknowledgement.

```c
esp_err_t scanner_display_set_awake(bool awake);
esp_err_t scanner_led_set_awake(bool awake);
/* State-model API, with structs declared in gateway_state_model.h. */
void gateway_state_record_result(gateway_state_t *, const gateway_result_t *);
void gateway_state_acknowledge_result(gateway_state_t *);
void gateway_state_stop(gateway_state_t *, gateway_phase_t, uint32_t error_code);
```

- [x] Migrate the QA actual-main fault harness: empty feeder cannot erase the last failed result; `file_saved && !scan.released` is a warning; acquisition+recovery failure leaves a responsive STOPPED loop and never starts another capture. Run the full host runner red.
- [x] Replace storage abort/return paths with that stopped loop. An acknowledgement clears an alert, not storage uncertainty. Only reviewed recovery actions may change storage state. Preserve diagnostics across normal polling and visual sleep; cold-power-loss persistence is not implied.
- [x] Add a static 64-entry diagnostic ring, at most 32 bytes per record: monotonic time, phase/event/error code, and two numeric values. Main writes it; callbacks queue bounded events. No SD status file or credentials. Display current stage and retained outcome; distinguish received bytes from published sizes.
- [x] Track requested versus confirmed visual state. Attempt backlight-off even after rendering is disabled. Retry failed off transitions at one-second intervals, at most three attempts; record exhaustion. New activity permits a fresh wake attempt. Routine unchanged polls/metrics must not extend the idle timer; active acquisition/capture/finalization never sleeps.
- [x] Test DMA timeout with backlight on, partial panel/backlight sleep, LED clear failure, bounded retry, wake failure, retained error after wake, wake-only button gestures, and five-minute normal idle using simulated time. Physical idle/wake remains R10 work. Run full tests/build. Commit `fix: retain scan outcomes and keep recovery UI responsive`.

## R7 — Explicit clock validity

**Depends on:** R6. **Files:** create `scanner_clock_model.c/.h`, `tests/test_scanner_clock_model.c`; modify `scanner_wifi.c/.h`, capture outcome, and display.

```c
typedef enum { SCANNER_CLOCK_UNKNOWN, SCANNER_CLOCK_MANUAL,
               SCANNER_CLOCK_NTP } scanner_clock_source_t;
typedef struct {
    bool valid;
    scanner_clock_source_t source;
    int64_t last_sync_monotonic_us;
    uint32_t last_error;
} scanner_clock_state_t;
scanner_clock_state_t scanner_clock_current(void);
```

- [x] Add red cases for missing time credentials, home AP/DNS/NTP timeout, and successful boot synchronization. An invalid clock must produce `TIME NOT SET` and mark the scan outcome accordingly.
- [x] Publish the existing boot sync result into this state; keep the existing bounded boot sequence. Do not infer current time from a saved old timestamp. A sync failure must not invalidate an already-valid running clock.
- [x] Keep time/network changes out of capture and unreleased writable sessions. Full manual resync/timezone controls are F3; this task provides the truthful status contract they consume.
- [x] Run clock/UI/full tests and build. Commit `fix: expose timestamp validity and synchronization failures`.

## R8 — Length-aware scanner status parsing

**Depends on:** R1. **Files:** modify `esci_scan.c` and `tests/test_esci_scan.c`.

- [x] Add leading/interior/trailing NUL, truncated token, valid-token-plus-junk, and reordered valid paper/battery token cases. Expected malformed result:

```c
assert(!status.valid);
assert(status.paper == ESCI_PAPER_UNKNOWN);
```

- [x] Record the actual received payload length and parse with pointer/end bounds. Reject embedded NUL and incomplete/unrecognized tokens without reading beyond the payload. Preserve valid no-paper, loaded, and low-battery combinations.
- [x] Run the full protocol runner, verify the new cases failed before the change and pass afterward, and commit `fix: parse scanner status by payload length`.

## R9 — Make the legacy host probe non-destructive

**Depends on:** R1. **Files:** modify `tools/probe_es60w.ps1`; create `tests/test_probe_es60w.ps1`; update README legacy-probe instructions.

- [x] Refactor external adapter/profile/netsh operations behind injectable wrappers and add mock tests for an existing scanner profile, absent profile, two adapters, connect failure, and restoration failure. No test may call real `netsh` or change an adapter.
- [x] Require an explicit `-InterfaceAlias`; query and restore that adapter's original connection. Preserve existing profiles instead of deleting them. Create/delete only a uniquely named temporary profile owned by the probe; securely remove its temporary XML in all paths. Preserve primary and restoration errors separately.
- [x] Validate the transaction with the mocked runner:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_probe_es60w.ps1
# Expected: pre-existing profiles unchanged; only selected adapter touched.
```

- [x] Parse all PowerShell scripts and commit `fix: preserve network profiles in legacy scanner probe`.

## R10 — Reliability release gate

**Depends on:** R1-R9. **Files:** update `README.md`, `docs/scanner-protocol-notes.md`, QA finding status; create `docs/qa/reliability-acceptance.md` with actual results.

- [x] Run full host tests, clean CI-equivalent build, size check, and source review of ownership/publication/error paths. Require a fitting image before flashing. Keep large feature code out of this release; flash expansion is P2.
- [ ] Prepare an expendable card and a private backup of any needed card contents through a verified stable reader. The unresolved recovery card is not a test fixture. Record firmware hash and build configuration.
- [ ] Flash the candidate and verify normal read-only mode, writable maintenance operations and hashes, paper blocked during maintenance, successful Windows safe eject, and explicit resume. Test prevented/rejected/aborted release sequences. Verify no APP mount before release.
- [ ] Scan a 20-page mixed corpus, including dark right-side content and narrow/full-width sheets, without reset. Verify originals and derivatives, actual byte sizes, timestamps/clock warnings, filenames, and no FAT errors. Exercise 600 dpi and quality 50 through controlled builds until the runtime profiles exist.
- [ ] On expendable media, test full-card and injected I/O failures plus controlled interruption during original/derivative publication. Check existing file hashes and FAT consistency after each fault. A decoder success alone is not sufficient.
- [ ] Check five-minute idle, button/page wake, scan-after-wake, failed outcome retention, missing/unmountable card at startup, stopped recovery UI, and headless display failure. Capture USB handoff logs through a verified independent diagnostic path where available.
- [ ] Record exact pass/fail evidence and unresolved issues. Release only when all P1 defects are closed and the storage/image gates pass. Commit `docs: record reliability release acceptance` and retain the candidate as the baseline for performance work.
