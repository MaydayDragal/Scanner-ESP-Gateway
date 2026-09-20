#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "storage_mode_model.h"

/* Call from the application task only. Close every /sdcard file before expose.
 * A failed transition revokes APP access; only restore_usb may recover it.
 */
esp_err_t usb_storage_start_app(void);
esp_err_t usb_storage_expose(void);
esp_err_t usb_storage_acquire(void);
esp_err_t usb_storage_restore_usb(void);
storage_mode_t usb_storage_mode(void);
/* Call only while idle. Changing protection always re-enumerates USB. */
esp_err_t usb_storage_enter_maintenance(void);
bool usb_storage_host_released(void);
esp_err_t usb_storage_resume_automatic(void);
bool usb_storage_capture_allowed(void);
bool usb_storage_transport_ready(void);
bool usb_storage_app_owned(void);
/* Expose/recovery wait up to 5 s for USB configuration. With no detected host,
 * ESP_OK means locally ready, with MSC left running for a later connection.
 * This getter reports host configuration, not the Windows volume-mount state. */
bool usb_storage_host_configured(void);

/* Last physical MSC read/write error since startup, ESP_OK before any error.
 * Sticky across successful I/O and ownership recovery; safe on either core. */
esp_err_t usb_storage_last_io_error(void);
