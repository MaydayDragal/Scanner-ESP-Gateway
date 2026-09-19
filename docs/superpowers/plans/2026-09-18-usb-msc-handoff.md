# Automatic USB Mass Storage Handoff Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace NCM/WebDAV with a read-only USB mass-storage drive that disconnects for every automatic scan and reappears with the completed file.

**Architecture:** A dedicated `usb_storage` module owns the SDMMC card, the TinyUSB MSC storage handle, USB descriptors, and all APP/USB ownership transitions. A small pure C model rejects unsafe or overlapping transitions. The main scan loop acquires application ownership before every filesystem access and returns ownership to USB afterward.

**Tech Stack:** ESP-IDF 5.5.5, `espressif/esp_tinyusb` 2.3.0, TinyUSB 0.21.0~2, SDMMC/FATFS, FreeRTOS, PowerShell hardware checks.

**Spec:** `docs/superpowers/specs/2026-09-18-usb-msc-handoff-design.md`

## Global Constraints

- Never mount the FAT volume in the application while TinyUSB can serve MSC requests.
- Expose MSC read-only with VID `0x303A`, PID `0x4002`, and `tud_msc_is_writable_cb()` returning false.
- Never format the card; preserve all existing JPG and TMP files.
- Keep automatic page detection, display, LED, 300 dpi RGB, JPEG quality 50, and 16 KiB capture chunks.
- Pin and verify the current `esp_tinyusb` behavior because safe teardown uses its TinyUSB task and driver lifecycle.
- Do not commit during execution unless the user separately asks; the current branch contains authorized uncommitted work.

---

### Task 1: Model Exclusive Storage Ownership

**Files:**
- Create: `main/storage_handoff_model.h`
- Create: `main/storage_handoff_model.c`
- Create: `tests/test_storage_handoff_model.c`
- Modify: `tests/run_protocol_tests.ps1`

**Interfaces:**
- Produces: `storage_handoff_t`, `storage_handoff_begin_to_app()`, `storage_handoff_complete_to_app()`, `storage_handoff_begin_to_usb()`, `storage_handoff_complete_to_usb()`, and `storage_handoff_begin_usb_recovery()`.
- State values: `STORAGE_APP`, `STORAGE_TO_USB`, `STORAGE_USB`, `STORAGE_TO_APP`, `STORAGE_ERROR`.

- [ ] **Step 1: Write the failing ownership test**

```c
#include "storage_handoff_model.h"
#include <assert.h>
int main(void) {
    storage_handoff_t state={.state=STORAGE_APP};
    assert(storage_handoff_begin_to_usb(&state));
    assert(!storage_handoff_begin_to_app(&state));
    storage_handoff_complete_to_usb(&state,true);
    assert(state.state==STORAGE_USB);
    assert(storage_handoff_begin_to_app(&state));
    assert(!storage_handoff_begin_to_usb(&state));
    storage_handoff_complete_to_app(&state,true);
    assert(state.state==STORAGE_APP);
    assert(storage_handoff_begin_to_usb(&state));
    storage_handoff_complete_to_usb(&state,false);
    assert(state.state==STORAGE_ERROR);
    assert(!storage_handoff_begin_to_app(&state));
    assert(storage_handoff_begin_usb_recovery(&state));
    storage_handoff_complete_to_usb(&state,true);
    assert(state.state==STORAGE_USB);
}
```

- [ ] **Step 2: Add the test to the host runner and verify RED**

Add a Zig C compile/run block matching the existing display and LED model tests.

Run: `powershell -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1`

Expected: compilation fails because `storage_handoff_model.h` does not exist.

- [ ] **Step 3: Implement the state model**

```c
typedef enum { STORAGE_APP, STORAGE_TO_USB, STORAGE_USB, STORAGE_TO_APP, STORAGE_ERROR } storage_handoff_state_t;
typedef struct { storage_handoff_state_t state; } storage_handoff_t;
```

Each normal `begin` succeeds only from its matching stable state. Each `complete` accepts only the corresponding transition and moves to the destination on success or `STORAGE_ERROR` on failure. Recovery may move only from `STORAGE_ERROR` to `STORAGE_TO_USB`; it can never grant application ownership.

- [ ] **Step 4: Verify GREEN**

Run: `powershell -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1`

Expected: all protocol, display, LED, DAV-path legacy, and ownership-model tests pass.

---

### Task 2: Build the MSC Storage Controller

**Files:**
- Create: `main/usb_storage.h`
- Create: `main/usb_storage.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/idf_component.yml`
- Modify: `sdkconfig.defaults`

**Interfaces:**
- Consumes: the Task 1 ownership model.
- Produces: `esp_err_t usb_storage_start_app(void)`, `esp_err_t usb_storage_expose(void)`, `esp_err_t usb_storage_acquire(void)`, `esp_err_t usb_storage_restore_usb(void)`, and `bool usb_storage_app_owned(void)`.

- [ ] **Step 1: Add the MSC-only build configuration**

Set:

```text
CONFIG_TINYUSB_MSC_ENABLED=y
# CONFIG_TINYUSB_NET_MODE_NCM is not set
```

Remove `usb_network.c`, `webdav.c`, and `esp_http_server` from `idf_component_register`. Add `usb_storage.c` and `storage_handoff_model.c`. Keep `espressif/esp_tinyusb: "^2.0.0"` and the LED dependency.

- [ ] **Step 2: Compile to verify the missing controller fails**

Run: `eim run 'idf.py build' v5.5.5`

Expected: build fails because `usb_storage.c` has not been created.

- [ ] **Step 3: Implement card and MSC initialization**

`usb_storage_start_app()` must:

1. Initialize the four-bit SDMMC bus on CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21 with internal pull-ups.
2. Install `tinyusb_msc` with `auto_mount_off=1` and a mount-completion callback.
3. Create the SDMMC storage at `TINYUSB_MSC_STORAGE_MOUNT_APP`, base path `/sdcard`, `max_files=10`, and `do_not_format=true`.
4. Initialize the ownership model to `STORAGE_APP` only after `/sdcard` is available.

Use the proven descriptor values:

```c
.idVendor=0x303A,
.idProduct=0x4002,
TUD_MSC_DESCRIPTOR(0,0,0x01,0x81,64)
```

Implement `tud_msc_is_writable_cb()` to return false for every LUN.

- [ ] **Step 4: Implement APP to USB handoff**

`usb_storage_expose()` must reject any state except `STORAGE_APP`, call `tinyusb_msc_set_storage_mount_point(...USB)`, verify the callback reports USB ownership, then install TinyUSB with the MSC descriptors. On failure, mark the transition failed and leave the application filesystem unavailable until recovery or reset.

- [ ] **Step 5: Implement USB to APP handoff**

`usb_storage_acquire()` must reject any state except `STORAGE_USB`. Queue a TinyUSB device-task callback through `usbd_defer_func()` from `device/usbd_pvt.h`; that callback calls `tud_disconnect()` and signals a semaphore. After the signal and a bounded host-detach delay, call `tinyusb_driver_uninstall()`, switch the storage mount point to APP, and verify `/sdcard` is mounted before completing the model transition.

Use bounded waits for both the deferred disconnect and MSC mount callback. A timeout returns `ESP_ERR_TIMEOUT` and moves the model to `STORAGE_ERROR`.

- [ ] **Step 6: Implement USB-only recovery**

`usb_storage_restore_usb()` is the only operation allowed from `STORAGE_ERROR`. It must close or uninstall any partial TinyUSB driver state, ensure APP FATFS is not mounted, set the MSC storage mount point to USB, and reinstall TinyUSB if needed. It must never mount FATFS for the application. Successful recovery returns the model to `STORAGE_USB`; failure remains `STORAGE_ERROR` and requires reset.

- [ ] **Step 7: Build and inspect the USB composition**

Run: `eim run 'idf.py build' v5.5.5`

Expected: build succeeds, the binary fits the app partition, and build configuration contains MSC with no NCM network interface.

Run: `rg -n "NCM|tinyusb_net|esp_http_server" build/config/sdkconfig.h build/esp-idf/main`

Expected: no active NCM, TinyUSB network, or HTTP server code in the application build.

---

### Task 3: Integrate Per-Scan Ownership Handoff

**Files:**
- Modify: `main/main.c`
- Modify: `main/scanner_display_model.c`
- Modify: `tests/test_scanner_display_model.c`

**Interfaces:**
- Consumes: `usb_storage_start_app()`, `usb_storage_expose()`, `usb_storage_acquire()`, and the existing `scanner_capture()`.
- Produces: a main loop that performs no `/sdcard` operation unless `usb_storage_app_owned()` is true.

- [ ] **Step 1: Add display-state regression assertions for USB handoff failures**

Use the existing `SCANNER_DISPLAY_ERROR` state with the exact detail text `USB STORAGE HANDOFF FAILED`. Assert the display model preserves that message without truncating it and sets the error tone.

Run: `powershell -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1`

Expected: the existing generic error rendering preserves the exact message and error tone. These assertions lock the integration contract before wiring it into `main.c`.

- [ ] **Step 2: Replace permanent FATFS/NCM startup**

In `app_main()`:

```c
ESP_ERROR_CHECK(usb_storage_start_app());
scanner_capture_result_t capture={.scan.message="Waiting for paper"};
write_status(scanner,capture,0);
ESP_ERROR_CHECK(usb_storage_expose());
```

Remove `init_card()`, `usb_network_start()`, `webdav_start()`, and every `webdav_set_scanning()` call. Keep `esp_netif_init()` and the default event loop for scanner Wi-Fi.

- [ ] **Step 3: Guard every scan with ownership transitions**

Immediately after a page trigger, set scanning display and LED state, then call `usb_storage_acquire()`. Start `scanner_capture()` only after it succeeds. Write `GATEWAY.TXT` while APP owns the card. Close all files before `usb_storage_expose()`.

If acquisition fails, report `USB STORAGE HANDOFF FAILED`, do not call capture, and call `usb_storage_restore_usb()`. If returning to USB fails, show `USB STORAGE RESTORE FAILED` and call the same USB-only recovery. Stop further scans only when recovery fails; never attempt application ownership again from an unrecovered error state.

- [ ] **Step 4: Preserve successful capture details across re-enumeration**

Keep the existing filename, byte count, duration, display completion, LED completion, quality 50 parameter, and final file synchronization logic. Do not reset the page trigger until storage has returned to USB.

- [ ] **Step 5: Run host tests and firmware build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1
eim run 'idf.py build' v5.5.5
git diff --check
```

Expected: all host tests pass, firmware builds, and diff check reports no whitespace errors.

---

### Task 4: Remove the Active WebDAV Path and Update Documentation

**Files:**
- Delete: `main/usb_network.c`
- Delete: `main/usb_network.h`
- Delete: `main/webdav.c`
- Delete: `main/webdav.h`
- Delete: `main/dav_path.h`
- Delete: `tests/test_dav_path.c`
- Delete: `tests/webdav_smoke.py`
- Delete: `tools/setup_usb_drive.ps1`
- Modify: `tests/run_protocol_tests.ps1`
- Modify: `README.md`
- Modify: `docs/scanner-protocol-notes.md`

**Interfaces:**
- Consumes: the completed MSC firmware behavior.
- Produces: one documented active USB path and no obsolete WebDAV tests in the default suite.

- [ ] **Step 1: Remove WebDAV-only source and tests**

Delete the files listed above and remove the DAV-path block from `tests/run_protocol_tests.ps1`. Preserve the historical NCM/WebDAV results in `docs/scanner-protocol-notes.md`.

- [ ] **Step 2: Rewrite the active README flow**

Document:

```text
ES-60W -- Wi-Fi Direct --> ESP32-S3 -- microSD --> read-only USB MSC --> Windows
```

State that the drive disappears during every scan, reappears afterward, and a copy in progress is interrupted by a newly inserted page. Retain build, flash, display, LED, quality 50, and image-verification instructions.

- [ ] **Step 3: Record the migration milestone**

Add a dated protocol-notes section describing the exclusive ownership design, PID `0x4002`, and the hardware checks that remain pending until Task 6.

- [ ] **Step 4: Verify repository references**

Run: `rg -n "usb_network_start|webdav_start|webdav_set_scanning|CONFIG_TINYUSB_NET_MODE_NCM" main README.md sdkconfig.defaults`

Expected: no active references.

---

### Task 5: Add a Safe Windows Migration Script

**Files:**
- Create: `tools/migrate_to_usb_msc.ps1`
- Modify: `README.md`

**Interfaces:**
- Produces: `tools/migrate_to_usb_msc.ps1 -DriveLetter S`, an idempotent administrative migration.

- [ ] **Step 1: Implement exact WebDAV mapping removal**

Use `[CmdletBinding(SupportsShouldProcess)]` and inspect `net use S:`. Remove it only if the remote path equals `\\192.168.77.1@80\DavWWWRoot`. Refuse to replace any other mapping or local volume.

- [ ] **Step 2: Restore the backed-up WebClient setting**

Read `backups/webclient-settings.json`. If `existed` is true, restore `FileSizeLimitInBytes`; otherwise remove only that registry value. Do not stop or disable WebClient because other applications may use it.

- [ ] **Step 3: Assign the MSC volume letter**

Locate the present USB device with `VID_303A&PID_4002`, resolve its `Win32_DiskDrive` and partition, and assign the requested free letter with `Set-Partition -NewDriveLetter`. If Windows already assigned the requested letter to that same volume, exit successfully.

- [ ] **Step 4: Exercise dry-run and refusal paths**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/migrate_to_usb_msc.ps1 -DriveLetter S -WhatIf
```

Expected before flashing: the script reports the exact WebDAV mapping cleanup and reports that the MSC device is not yet present without changing the machine.

---

### Task 6: Review, Flash, Migrate, and Verify Hardware

**Files:**
- Modify after results: `docs/scanner-protocol-notes.md`
- Modify after results: `README.md`

**Interfaces:**
- Consumes: the built firmware and Windows migration script.
- Produces: verified repeated MSC scan cycles on the physical device.

- [ ] **Step 1: Run the final software gates**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_protocol_tests.ps1
eim run 'idf.py build' v5.5.5
git diff --check
Get-FileHash build/scanner_esp_gateway.bin -Algorithm SHA256
```

Expected: all tests and build pass; record the binary size and SHA-256.

- [ ] **Step 2: Request focused code review**

Review storage transition ordering, failure paths, TinyUSB task teardown, read-only enforcement, filesystem access guards, and preservation of display/LED/quality behavior. Resolve every Critical or Important finding and repeat Step 1.

- [ ] **Step 3: Flash the board**

Put the ESP32-S3 in download mode and run:

```powershell
eim run 'idf.py -p COM3 flash' v5.5.5
```

Verify esptool reports the application image hash and hard reset completes.

- [ ] **Step 4: Verify idle MSC and migrate S:**

Tap RESET without BOOT. Run `tests/usb_msc_smoke.ps1`, then run `tools/migrate_to_usb_msc.ps1 -DriveLetter S`. Confirm `S:` is a local USB volume for PID `0x4002`, existing scans remain, and Windows rejects file creation and deletion.

- [ ] **Step 5: Verify two automatic scan cycles**

For each of two pages, observe that S: disappears after feeder detection, the display and LED show scanning, and S: returns without resetting the ESP. Confirm a new sequential JPG appears after each cycle.

- [ ] **Step 6: Decode and compare the new scan**

Copy the newest JPG to a fixed verification path and run:

```powershell
$latest=Get-ChildItem S:\SCAN*.JPG | Sort-Object LastWriteTime | Select-Object -Last 1
Copy-Item -LiteralPath $latest.FullName -Destination backups/msc-latest.jpg
python tests/verify_scan.py backups/msc-latest.jpg
```

Expected: full decode passes at 2550 x 4200 RGB, 300 dpi, with the verified Epson quality-50 tables.

- [ ] **Step 7: Record actual hardware results**

Update the README and protocol notes with enumeration behavior, drive letter, two produced filenames, sizes, durations, decode result, any observed reconnect delay, and the final firmware SHA-256.
