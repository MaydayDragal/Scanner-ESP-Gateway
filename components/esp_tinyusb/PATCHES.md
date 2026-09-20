# Scanner gateway esp_tinyusb override

Base: Espressif **esp_tinyusb 2.3.0**, copied from the resolved registry package.
Upstream repository: https://github.com/espressif/esp-usb
Commit: `0a1e5fd0f7f9d3c58a0d4949b5d1bfbf23af8d96`, path `device/esp_tinyusb`.
Registry component hash:
`0fab24246e07bdf919c6489d17a590cffbffbbf6a9c44d6235641645c8b946b7`.

`LICENSE`, upstream copyright notices, `idf_component.yml`, `.component_hash`, and
`CHECKSUMS.json` are preserved. The last two describe the original registry package,
not the patched tree. `UPSTREAM_SHA256.json` additionally records SHA-256 for every
original file, allowing each local modification to be distinguished from upstream.
All original files are retained. Patched upstream files are `tinyusb_msc.c`,
`include/tinyusb_msc.h`, `storage_sdmmc.c`, and `CMakeLists.txt`.

## Patches

1. `tinyusb_msc.c`: WRITE10 calls the existing synchronous sector writer and returns
   accepted bytes only after the medium (SDMMC: `sdmmc_write_sectors`) returns
   `ESP_OK`. Errors return `TUD_MSC_RET_ERROR`. The existing 512-byte configuration
   and oversize rejection remain. Remove the copied write buffer, deferred write
   functions and pending-write count. Track active synchronous reads/writes instead
   so deletion refuses while a callback holds or awaits the storage mutex.
2. `tinyusb_msc.c`: publish a stack-local `IO_ERROR` event carrying LUN, operation,
   and the original `esp_err_t` for read/write failures. No I/O error job is queued,
   and error publication does not mount or render. The project callback records
   only an atomic sticky error and never treats this event as mount completion.
3. `include/tinyusb_msc.h`: append `TINYUSB_MSC_EVENT_IO_ERROR` without changing
   previous event values; add its payload and read/write operation enum. Document
   the bounded callback contract and synchronous storage deletion lifetime check.

4. Mount helpers publish APP ownership only after a successful filesystem mount;
   refusing format returns the mount error. The mount setter returns helper errors
   and retains the previous owner. The SD adapter propagates failed FAT unmount.
   Already-removed disk/VFS registrations are accepted as idempotent cleanup, so
   stopped recovery can reconcile a partial unmount without granting APP access.
5. A task-owned accepted-eject latch requires removal permitted and no latched I/O
   failure. PREVENT/ALLOW uses the dedicated callback. Accepted eject blocks later
   READ10/WRITE10. Release requires a matching successful CSW completion in the same
   generation. `tud_event_hook_cb` invalidates on every bus reset/unplug using a
   lock-free atomic generation and no storage access, allocation, mutex, or UI.
   Explicit reconnect, transport error, and BOT reset also invalidate evidence.
6. SYNCHRONIZE CACHE(10) waits on the synchronous medium-operation mutex and fails
   on sticky I/O error. It does not eject, clear errors, or mount APP.
7. `CMakeLists.txt` derives `scanner_msc_device.c` in the build directory using
   `patch_msc_core.py`. The script requires exact upstream TinyUSB 0.21.0~2 MSC SHA-256
   `9bba988f5c1e83db1d565e55e934a8e9dd7f5e5085820b6c555ffa1484473ac5` and unique
   replacement anchors. It replaces exactly one MSC source in the TinyUSB target;
   a missing/duplicate unit, changed hash, or changed anchor fails configuration.
   Upstream managed and offline-vendored core bytes remain immutable.

The generated core calls `tinyusb_msc_command_status_cb(lun, cdb, success)` before
its original completion callback. Success requires PASSED CSW, successful transfer,
correct IN endpoint, and exactly 13 transferred bytes. Failed/short/aborted transfers
cannot publish release, even though the original core ignores transfer results.
The generated core rejects failed/aborted transfers before command/data parsing,
stalls both endpoints and requires ordinary BOT reset/clear-halt recovery. It also
invalidates on class reset and BOT reset.
This hook supplements the accepted-eject latch; a malformed CBW can accept the
eject callback yet later send FAILED CSW. The ordinary completion callback alone
cannot distinguish that case. Host tests compile the same generated unit.

The application disables automatic mounting. No eject/reset/disconnect callback
mounts APP. Resume additionally requires current release evidence after the USB
task parks, then teardown and a successful filesystem mount before APP access.

## Resolution and verification

`main/idf_component.yml` selects version `==2.3.0` with
`override_path: ../components/esp_tinyusb`. It independently pins TinyUSB to
`==0.21.0~2`; do not replace that pin with the upstream component's broad range.
Run `python tests/run_usb_storage_tests.py` to compile the patched component, its
SDMMC adapter, the production controller and the generated pinned TinyUSB
MSC command processor. Hardware calls are synthetic SDK boundaries. Cases exercise
actual CBW/data/CSW processing and FIFO detach ordering, including physical timeout,
late task acknowledgement, recovery and refusal to delete active storage.

The pinned TinyUSB core may overwrite sense with generic medium-not-present data
on callback failure. The required host-visible contract is a **FAILED CSW**, with
correct residue. The exact local SDK error is retained separately.

Synchronous completion reflects the SDMMC driver's result; it does not guarantee
survival of arbitrary card-internal caching or power loss. No hardware fault testing
or throughput claim is implied by the host tests.
