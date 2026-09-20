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
All original files are retained; only the two source files listed below are patched.

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

All other behavior and source files retain upstream contents. The application
disables automatic mounting; a SCSI eject request therefore cannot transfer local
filesystem ownership in its callback. The application still requires an acknowledged,
parked TinyUSB task before teardown and an explicit mount completion before APP access.

## Resolution and verification

`main/idf_component.yml` selects version `==2.3.0` with
`override_path: ../components/esp_tinyusb`. It independently pins TinyUSB to
`==0.21.0~2`; do not replace that pin with the upstream component's broad range.
Run `python tests/run_usb_storage_tests.py` to compile the patched component, its
unmodified SDMMC adapter, the production controller and the exact pinned TinyUSB
MSC command processor. Hardware calls are synthetic SDK boundaries. Cases exercise
actual CBW/data/CSW processing and FIFO detach ordering, including physical timeout,
late task acknowledgement, recovery and refusal to delete active storage.

The pinned TinyUSB core may overwrite sense with generic medium-not-present data
on callback failure. The required host-visible contract is a **FAILED CSW**, with
correct residue. The exact local SDK error is retained separately.

Synchronous completion reflects the SDMMC driver's result; it does not guarantee
survival of arbitrary card-internal caching or power loss. No hardware fault testing
or throughput claim is implied by the host tests.
