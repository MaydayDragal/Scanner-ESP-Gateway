# Measured performance implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce measured scan/transfer latency and resource use without weakening the reliability release.

**Architecture:** Establish per-stage measurements, then vary one bottleneck at a time. Keep exclusive ownership, validated originals, synchronous error-reporting MSC writes, and bounded memory. Reserve flash space before large features.

**Tech Stack:** Existing ESP-IDF/C firmware and host tests, controlled benchmark builds, read/write/content hashes, heap/stack telemetry.

**Spec:** [Design brief](../specs/2026-09-19-qa-improvements-design.md). Prerequisite: [R10 reliability gate](2026-09-19-reliability.md).

## Global constraints

- Target the existing Waveshare ESP32-S3-LCD-1.47 and Epson ES-60W; verify the physical board revision before enabling new pins or PSRAM.
- Use ESP-IDF 5.5.5 and TinyUSB 0.21.0~2; maintain a checked-in, narrowly patched esp_tinyusb 2.3.0 override.
- Default scanning remains 300 dpi RGB, JPEG quality 75, with a 300-second display/LED timeout.
- Only one owner may access the SD filesystem; never format automatically or overwrite an existing scan or working file.
- Keep credentials and private firmware binaries out of Git, CI artifacts, logs, and published reports.
- Do not recreate GATEWAY.TXT or require a companion PC application.
- Use expendable media for write-fault and power-loss testing; the previously damaged card is excluded.

## Review focus

- A larger USB buffer must preserve partial/failing I/O semantics: P3.
- Noise-heavy 600 dpi JPEGs must not trigger row-sized unbounded allocations: P4.
- A host-created filename after maintenance must defeat stale allocation hints safely: P6.
- UI DMA buffers must not be reused before transfer completion: P7.
- Power-saving changes must not break USB enumeration or miss newly inserted pages: P8.

## Shared measurement contract

Create `docs/qa/performance-results.md` in P1. Each experiment records firmware/configuration hash, card/board, profile, page/fixture identity, USB host, repeat count, median and worst observed time, peak workspace, minimum free heap/largest block, minimum stack headroom, and all errors. Do not claim a speedup from different documents or quality settings.

For timing experiments, use at least five repetitions per selected fixture/profile after one warm-up, compare against R10 or the last accepted optimization, and show spread. Retain a speed optimization only for a repeatable improvement of at least 5% in its target stage with no correctness failure, no >5% regression in end-to-end latency, and no breached memory/stack budget. Power changes use measured current and explicit page-detection limits. A no-improvement result closes the experiment with the previous default retained.

The existing USB limit is a bus limit, not a performance target: [Espressif documents 12 Mbit/s full speed](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html). Actual file throughput includes protocol and SD overhead.

## P1 — Instrument phases before tuning

**Files:** extend `gateway_diagnostics.c/.h`, `scanner_capture.c/.h`, `usb_storage.c`, `main.c`; create `tests/test_gateway_metrics.c`, `docs/qa/performance-results.md`.

**Interface:** declare `gateway_metrics_t` with 64-bit monotonic durations for connect, receive/write, inspect, derivative, publication, and USB restore; 32-bit received/original/derivative bytes; min heap/largest free block/stack headroom. Metrics contain no image data or credentials.

- [ ] Add a deterministic fake-clock test: timestamp differences match the scripted phase boundaries, missing phases are explicitly absent, and wrap/overflow is not silently truncated.
- [ ] Add begin/end sampling around existing boundaries and snapshot through diagnostics. Sampling does not access FAT while USB owns it and does not count as user activity.

```c
/* In tests/test_gateway_metrics.c, after a scripted 25 ms receive stage. */
assert(metrics.receive_write_us == 25000);
assert(metrics.received_bytes != metrics.derivative_bytes);
```

- [ ] Run the full host runner and firmware build; collect baseline timings for a narrow sheet, full-width text, and a noisy/dark image at 300/75 and 600/75, with a quality-50 comparison where relevant.
- [ ] Record the measurements and commit `perf: add bounded per-stage gateway metrics`.

## P2 — Expand application flash with a deliberate migration

**Files:** create `partitions.csv`, `tests/test_partition_layout.py`, `docs/qa/flash-migration.md`; modify `sdkconfig.defaults`, README, and build configuration.

**Proposed layout:** preserve current NVS/PHY addresses and the current application start; verify them against the installed table before flashing. The remaining flash stays unallocated.

```csv
# Name, Type, SubType, Offset, Size
nvs,data,nvs,0x9000,0x6000
phy_init,data,phy,0xF000,0x1000
ota_0,app,ota_0,0x10000,0x400000
ota_1,app,ota_1,0x410000,0x400000
otadata,data,ota,0x810000,0x2000
```

- [ ] Add layout tests for overlap, alignment, 16 MiB bounds, unchanged NVS/PHY/app-start addresses, both 4 MiB app slots, and 8 KiB OTA data. Run `python tests/test_partition_layout.py` red before adding the layout.
- [ ] Select the custom table and compile with the locked toolchain. F8 will enable and test application rollback/update policy; this task only establishes the layout and a bootable baseline.
- [ ] Before hardware migration, read/record the installed partition table and keep private backups of needed flash contents. If addresses differ, stop this migration and update the reviewed map; do not erase the whole chip or overwrite unknown data.
- [ ] Flash the matching partition table, bootloader, and baseline application through the established BOOT/RESET process. Initialize the new OTA data partition deliberately; verify boot selection and unchanged NVS data. Application updates will not rewrite this table.
- [ ] Run R10's normal scan, maintenance/eject, and sleep/wake smoke checks. Commit `build: reserve application slots for feature growth` with the actual migration evidence.

## P3 — Benchmark larger MSC transfers

**Files:** benchmark-only sdkconfig overlays under `tests/bench/`; `docs/qa/performance-results.md`; change `sdkconfig.defaults` only for the measured winner.

- [ ] Run the R2 write/error/detach regressions at 512, 4096, 8192, and 16384-byte MSC buffers. Include non-full final transfers, sector alignment, write failure, and blocked physical I/O. Fix no failing invariant by dropping the test.
- [ ] Build separate named output directories for each overlay. Transfer identical synthetic files in automatic read-only mode and while Windows owns writable maintenance mode; verify hashes and repeat with background host reads, then safe-eject transitions. Host transfers stop before release permits APP access.

```text
candidate -> all correctness cases pass -> repeated transfer measurements
          -> choose smallest buffer with the accepted repeatable gain
```

- [ ] Include memory and stack results. Do not add asynchronous write queues merely to increase speed. Keep 512 bytes if gains are insufficient or resource use is worse.
- [ ] Commit the evidence and, only when supported, the selected default as `perf: tune MSC transfer size from measurements`.

## P4 — Consolidate JPEG work and memory

**Files:** `jpeg_stream.c/.h`, both crop modules, `scanner_capture.c`; extend `tests/test_jpeg_pipeline.py` and allocation/I/O counters.

- [ ] Starting from R5's streaming validator, add tests counting source bytes read and peak JPEG workspace. Preserve every corruption and retained-content regression.
- [ ] Reuse the validated metadata and crop statistics instead of rereading for an independent height scan/index. Keep a single bounded reader and streaming derivative writer; copy/patch headers in chunks. Do not restore fixed 64 KiB header or largest-row allocations.

```text
one complete inspect/statistics pass -> normalize original -> publish
                                    -> optional derivative streaming pass
```

- [ ] Check the <=32 KiB JPEG workspace target at 300/600 dpi, including noise-heavy rows and allocation failures. Measure both total SD traffic and runtime; counters alone do not prove faster capture.
- [ ] Verify the physical board and run a memory test before considering its documented PSRAM. If enabled, place only suitable bulk scratch data there; retain DMA buffers and driver-critical allocations in required internal memory. Treat PSRAM as a separate measured configuration, not a prerequisite for correctness.
- [ ] Run the image/capture suites and hardware corpus. Commit accepted changes/evidence as `perf: reuse validated JPEG metadata and bounded workspace`.

## P5 — Update image bookkeeping per chunk

**Files:** `esci_scan.c`, `tests/test_esci_scan.c`.

- [ ] Add equivalence tests for all splits of a small SOI/data/EOI fixture, including one-byte chunks, markers split across callbacks, zero-length input, and count overflow. Pin first two, last two, and total received bytes.
- [ ] Replace the byte-by-byte bookkeeping loop with bounded first/last-byte extraction and a checked length addition. Do not change byte streaming, image limits, parser timeouts, or failure cleanup.
- [ ] Run protocol tests and benchmark CPU time on fixed synthetic image chunks at identical sizes. Commit only with equivalent behavior and measured gain: `perf: update JPEG stream bookkeeping per chunk`.

## P6 — Faster collision-safe naming

**Files:** `scan_files.c/.h`, capture tests; later F4 adds user prefixes/folders on this interface.

- [ ] Add a filesystem lookup counter to tests with 0, 100, 1000, and 9999 occupied names; simulate Windows adding/deleting a reserved-looking name during maintenance.
- [ ] Build the next-name hint when APP ownership is obtained and reconcile it after every writable session. A hint is never a reservation: perform the full R4 collision checks and exclusive scratch creation on every attempt. Handle exhaustion explicitly without wrapping onto existing data.

```text
cache hint -> check all original/derivative/legacy names -> exclusive create
host maintenance ends -> invalidate hint -> rescan while APP owns volume
```

- [ ] Prove reduced lookups on repeated captures and identical collision/failure behavior. Benchmark full directories on expendable storage, then commit `perf: cache scan numbering with ownership-aware invalidation`.

## P7 — Draw only changed regions

**Files:** `scanner_display.c`, `scanner_display_model.c/.h`; create `tests/test_display_dirty_regions.c`; preserve R6 visual-fault tests.

- [ ] Add tests mapping old/new views to dirty rectangles. A wake, panel reset, orientation change, or invalid prior frame must force a full redraw.
- [ ] Cache only the last successfully drawn view. Redraw changed rows/regions and cap progress refresh to at most five times per second; stage/error changes render promptly. Continue using the existing strip buffer and wait for DMA completion before reuse.
- [ ] Test partial transfer failure invalidates the cache and retains fail-dark behavior. Measure bytes sent and capture timing; inspect the physical display for stale text and tearing.
- [ ] Commit if improvement passes the shared gate: `perf: redraw changed scanner status regions`.

## P8 — Measure idle power and response

**Files:** `scanner_wifi.c`, `main.c` poll scheduling, settings defaults once F1 exists; `tests/test_gateway_poll_schedule.c`; performance evidence.

- [ ] Add schedule tests for normal readiness, visual sleep, loaded paper, disconnection, active capture, and maintenance. Timing uses monotonic deadlines and cannot trigger filesystem operations while USB owns it.
- [ ] Compare the current two-second polling/no-modem-sleep baseline with supported modem sleep and a five-second idle polling candidate. Immediately return to the normal cadence after button, paper, or status activity. Do not introduce ESP deep sleep while USB must remain available.
- [ ] Measure supply current with suitable hardware and at least ten page-detection trials per candidate. Accept only stable USB/Wi-Fi, no missed pages, and <=6 seconds worst observed page detection in the low-power state. If current measurement is unavailable, do not claim or enable a power optimization based on guesswork.
- [ ] Run sleep/wake, maintenance/eject, and scan-after-idle tests. Record the chosen policy and commit `perf: select measured idle power policy` or the no-change evidence.

## Release gate

- [ ] Every retained optimization passes its focused tests and the reliability suite; a fresh build fits the selected partition.
- [ ] Hardware hashes, image content, FAT checks, and five-minute visual sleep remain correct.
- [ ] `performance-results.md` separates measured wins, unchanged defaults, and hardware-dependent experiments with explicit evidence.
- [ ] Tag/record the accepted performance baseline before enabling user features.
