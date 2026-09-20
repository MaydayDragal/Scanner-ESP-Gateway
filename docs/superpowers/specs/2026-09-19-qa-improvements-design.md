# QA improvements: design brief

Status: proposed implementation design; no firmware changes have been made by this planning task.

## Purpose and scope

Make the ESP32-S3/ES-60W gateway dependable for ordinary scanning, then improve measured performance and add the capabilities identified in the [QA report](../../qa-review-2026-09-19.md). Cover all 15 QA findings, all nine optimization candidates, and all 12 proposed features. The user's earlier preferences remain: work runs on the ESP, files are available through simulated USB storage, mixed page widths are supported, and the display and LED sleep after five minutes by default.

## Global constraints

- Target the existing Waveshare ESP32-S3-LCD-1.47 and Epson ES-60W; verify the physical board revision before enabling new pins or PSRAM.
- Use ESP-IDF 5.5.5 and TinyUSB 0.21.0~2; maintain a checked-in, narrowly patched esp_tinyusb 2.3.0 override.
- Default scanning remains 300 dpi RGB, JPEG quality 75, with a 300-second display/LED timeout.
- Only one owner may access the SD filesystem; never format automatically or overwrite an existing scan or working file.
- Keep credentials and private firmware binaries out of Git, CI artifacts, logs, and published reports.
- Do not recreate GATEWAY.TXT or require a companion PC application.
- Use expendable media for write-fault and power-loss testing; the previously damaged card is excluded.

## Recommended product decisions

These decisions make the plan concrete and are visible for review before implementation.

1. **Automatic mode:** Windows sees a read-only USB drive. New pages still trigger scans and the drive still reconnects after each page. Read operations may be interrupted by that documented reconnect.
2. **Writable maintenance:** the user selects a separate mode on the ESP. Windows can add, rename, and delete files; automatic acquisition is suspended. Resume only after an accepted host eject has completed, followed by a local resume action. A disconnect, timeout, reset, or idle interval is not proof of safe release.
3. **Original preservation:** `SCAN0001.JPG` is a fully validated, full-width original with the scanner's excess height metadata corrected. Optional width cropping produces `CROP0001.JPG`; it never replaces or deletes the original. Both names are FAT 8.3 compatible. This roughly doubles storage for pages that receive a cropped derivative.
4. **Scan result:** readiness and last outcome are independent. A saved original with failed crop or scanner cleanup is visible as saved-with-warning. Fatal storage uncertainty stops captures but keeps the UI operating.
5. **Defaults and profiles:** initially offer only tested 300/600 dpi and quality 50/75 combinations. A job takes an immutable settings snapshot. A menu change never changes a page already being scanned.
6. **Unknown time:** display `TIME NOT SET`; retain an explicit clock-valid flag. Do not present a guessed historical timestamp as synchronized. Resync never switches networks during capture or an unreleased writable session.
7. **Document features:** multipage PDF embeds validated JPEG data without recompression. Originals remain available even if PDF generation fails. No OCR requirement is introduced.
8. **Updates:** use a user-selected signed application image placed on SD in writable maintenance mode, followed by safe eject. The ESP writes an inactive firmware slot and supports rollback. Partition/bootloader migration remains a separate, deliberate flashing step.

## Architecture

Keep `main` as the single owner of scanner sessions, filesystem operations, and UI state. A button sampler may enqueue events; it cannot mount storage, switch Wi-Fi, render, or alter a live job. USB callbacks only perform their class I/O and publish bounded state/error events. They never run controller transitions or UI code.

Use synchronous SD writes in the first corrected MSC implementation. This removes the hidden deferred SD-write queue and lets the existing boundary between callbacks serve as a real I/O boundary. Failed writes return failed USB command status and a precise local diagnostic. The pinned TinyUSB stack may report generic host sense; do not promise precise host sense without another reviewed patch.

Separate storage ownership from user mode and connection-session generation. Safe eject requires both an accepted eject command and completion of its status transfer in the same session. Reset clears pending eject evidence. Block new media operations after accepted eject.

Capture is a transaction with independently reserved original and derivative names. Validate every JPEG row before publication. Keep the original on derivative failure and report actual published bytes. Bound memory and file sizes before allocation; do not decode a whole 600 dpi page into RAM.

Use small state models and existing host-test patterns rather than introducing a large GUI framework. Add restricted event servicing to long operations when interactive features need it. State-changing commands remain deferred until the owner is idle.

## Success criteria

- Every accepted USB write reaches the card or fails visibly; no accepted write is lost at handoff.
- Host write sessions cannot overlap ESP filesystem access.
- Dark documents, malformed JPEGs, and filename collisions cannot destroy the only complete original.
- Faults identify the failed stage and persist independently of feeder state; boot and fatal recovery remain inspectable.
- Host tests run from a clean checkout, a fresh firmware build succeeds, and physical acceptance uses hash/content/FAT checks on expendable media.
- Each optimization has before/after measurements and is retained only if it improves its intended metric without a correctness or resource regression.
- Feature releases retain the preceding release's storage and image-preservation guarantees.

## Design sources

The [QA report](../../qa-review-2026-09-19.md) is the evidence record. Its host reproductions do not prove the cause of the earlier hardware FAT incident.

The non-B [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/ESP32-S3-LCD-1.47_schematic_diagram.pdf) identifies the BOOT input as IO0; verify the installed board before using it. Use input-only runtime handling and wait for release after startup.

ESP-IDF documents inactive-slot application updates and boot validation/rollback in its [5.5.5 OTA guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/ota.html). This plan uses those application APIs for SD-delivered updates; it does not treat live partition-table replacement as a safe application update.
