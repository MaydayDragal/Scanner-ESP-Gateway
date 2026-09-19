# Automatic USB Mass Storage Handoff Design

## Goal

Replace USB NCM and WebDAV with a read-only USB mass-storage drive. Keep automatic page detection, the onboard display and RGB status LED, 300 dpi RGB capture, scanner JPEG quality 50, and 16 KiB streaming writes. For every detected page, the USB drive disappears during capture and returns with the completed scan visible.

## Ownership Rule

The ESP and Windows must never access the FAT volume at the same time. A single storage controller owns the microSD card in one of two states:

- `USB_OWNED`: the ESP filesystem is unmounted and TinyUSB MSC exposes the card read-only to Windows.
- `APP_OWNED`: TinyUSB is disconnected, MSC has released the card, and FATFS is mounted at `/sdcard` for capture.

Every transition waits for an explicit completion event. A timeout or failed transition enters an error state and does not grant the other side access.

## Idle and Scan Flow

At startup, firmware initializes the card, briefly mounts it for status recovery if required, then transfers it to `USB_OWNED`. Scanner Wi-Fi and feeder status polling continue without mounting the filesystem.

When the debounced page trigger fires:

1. Set the display to scanning and the LED to blue.
2. Disconnect TinyUSB from the host on the TinyUSB task.
3. Wait until the host-facing MSC path is stopped.
4. Ask the TinyUSB MSC storage manager to transfer the card to the application and wait until FATFS is available at `/sdcard`. The storage manager performs the mount without formatting.
5. Capture to the next unused `SCANnnnn.TMP` with 16 KiB chunks.
6. Validate JPEG boundaries and scanner page/job completion.
7. Flush and synchronize the file, close it, and rename it to `SCANnnnn.JPG` only on success.
8. Write `GATEWAY.TXT` and close all files.
9. Ask the MSC storage manager to unmount FATFS and transfer the card back to USB, then reconnect TinyUSB and wait for the USB drive to enumerate.
10. Show completion or error details while returning the LED to the matching state.

Failed captures retain an incomplete `.TMP` file. The firmware still returns the card to USB ownership so the device remains recoverable. Because MSC exposes the complete FAT volume, TMP files may be visible in Windows even though only JPG names represent successful scans.

## USB Interface

The active USB configuration contains one MSC interface using the existing prototype VID `0x303A` and MSC PID `0x4002`. The SCSI writable callback always returns false. NCM, lwIP USB networking, the HTTP server, and WebDAV are excluded from the build.

The drive is physically disconnected during every scan. Windows may display an open Explorer window as unavailable until re-enumeration completes. A copy already in progress will fail if a new page triggers capture; the source file remains intact on the SD card and can be copied again after reconnection.

## Display and LED

The current display model remains the source of user-visible state. While idle it shows scanner connectivity, feeder state, battery warning, and the current `300 DPI | RGB | JPG 50` setting. During handoff and capture it shows scanning progress. Completion retains filename, size, and duration.

The RGB LED keeps the approved mapping. Blue covers the entire USB disconnect, ownership transfer, and capture interval. Red indicates capture or storage transition failure. Low battery remains orange except while scanning or reporting an error.

## Windows Migration

The setup removes the persistent WebDAV `S:` mapping, restores the backed-up WebClient file-size setting when available, and no longer requires WebClient or the static USB network address. After the MSC volume enumerates, Windows may assign any free drive letter. The migration script assigns `S:` to the USB volume when available and never replaces another local disk or occupied letter.

Existing scan data on the card is preserved. Firmware never formats the card.

## Failure Handling

- Failure to leave USB ownership: do not mount FATFS or start a scan; report an error and attempt to restore MSC.
- FAT mount failure: do not scan; release application ownership and restore MSC.
- Scanner, network, or SD write failure: close the stream, retain TMP, write status when possible, then unmount and restore MSC.
- Failure to return to MSC: remain unmounted, show red, and require a reset rather than risk simultaneous access.
- USB host absence: ownership transitions still complete locally; the next connection enumerates the current card contents.

## Tests

Host tests continue to cover fragmented ESC/I transfers, exact quality-50 parameters, 16 KiB save chunks, status parsing, display formatting, LED priority, and write-failure cleanup.

Firmware build verification checks the MSC-only USB configuration and excludes NCM/WebDAV sources. Hardware acceptance verifies:

1. Read-only MSC enumeration while idle.
2. Automatic drive disappearance when a page triggers.
3. Automatic reappearance after capture without resetting the ESP.
4. At least two consecutive page cycles.
5. A new quality-50 JPEG copied from the drive and fully decoded at 2550 x 4200 RGB and 300 dpi.
6. Existing scans remain present and unchanged.
7. Windows rejects file creation and deletion on the exposed volume.

## Constraints

USB remains full speed. Quality 50 reduces typical scan files substantially, but this design does not promise high-speed USB throughput. Windows drive reconnection time depends on its USB storage and volume-mount behavior.
