#include "usb_storage.h"

#include <stdio.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
/* Deliberate private API: esp_tinyusb 2.3.0 / TinyUSB 0.21.0~2 in dependencies.lock. */
#include "device/usbd_pvt.h"
#include "storage_handoff_model.h"

#define MOUNT_TIMEOUT_MS 2000
#define DISCONNECT_TIMEOUT_MS 2000
#define HOST_DETACH_MS 250
#define HOST_CONFIG_TIMEOUT_MS 5000

static const char *TAG = "usb_storage";
static storage_handoff_t ownership = {.state = STORAGE_ERROR};
static sdmmc_card_t card;
static tinyusb_msc_storage_handle_t storage;
static SemaphoreHandle_t mount_done;
static SemaphoreHandle_t disconnect_done;
static SemaphoreHandle_t host_changed;
static tinyusb_msc_event_t mount_event;
static bool started;
static bool driver_installed;
static bool driver_uncertain;
static bool recovery_failed;
static bool disconnected;
static bool task_quiesced;
static atomic_bool host_configured;
static atomic_bool usb_writable;
static atomic_int last_io_error = ESP_OK;

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
};
static const char *strings[] = {"\x09\x04", "Espressif", "Scanner USB Storage", "SCANNER001"};
static tinyusb_config_t usb_config;

bool tud_msc_is_writable_cb(uint8_t lun)
{
    return lun == 0 && atomic_load(&usb_writable);
}

static void storage_event(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    (void)handle;
    (void)arg;
    switch (event->id) {
    case TINYUSB_MSC_EVENT_IO_ERROR:
        atomic_store(&last_io_error, event->io_error.error);
        break;
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
    case TINYUSB_MSC_EVENT_FORMAT_FAILED:
        mount_event = *event;
        xSemaphoreGive(mount_done);
        break;
    case TINYUSB_MSC_EVENT_MOUNT_START:
    default:
        break;
    }
}

static bool app_mounted(void)
{
    struct stat info;
    return stat("/sdcard", &info) == 0 && S_ISDIR(info.st_mode);
}

static esp_err_t wait_mount(tinyusb_msc_mount_point_t point)
{
    if (xSemaphoreTake(mount_done, pdMS_TO_TICKS(MOUNT_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (mount_event.id != TINYUSB_MSC_EVENT_MOUNT_COMPLETE || mount_event.mount_point != point) {
        return ESP_FAIL;
    }
    return app_mounted() == (point == TINYUSB_MSC_STORAGE_MOUNT_APP) ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_mount(tinyusb_msc_mount_point_t point)
{
    xSemaphoreTake(mount_done, 0);
    esp_err_t err = tinyusb_msc_set_storage_mount_point(storage, point);
    return err == ESP_OK ? wait_mount(point) : err;
}

static void usb_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED || event->id == TINYUSB_EVENT_DETACHED) {
        atomic_store(&host_configured, event->id == TINYUSB_EVENT_ATTACHED);
        xSemaphoreGive(host_changed);
    }
}

static esp_err_t wait_host_configuration(void)
{
    const TickType_t limit = pdMS_TO_TICKS(HOST_CONFIG_TIMEOUT_MS);
    TickType_t started_at = xTaskGetTickCount();
    for (;;) {
        /* The stack can configure before driver_install publishes event_cb.
         * Its current configuration also covers that early callback race. */
        if (tud_mounted()) {
            atomic_store(&host_configured, true);
            ESP_LOGI(TAG, "USB host configured MSC");
            return ESP_OK;
        }
        TickType_t elapsed = xTaskGetTickCount() - started_at;
        if (elapsed >= limit) break;
        xSemaphoreTake(host_changed, limit - elapsed);
    }
    atomic_store(&host_configured, false);
    if (!tud_connected()) {
        /* This board has no VBUS presence sensor. No protocol connection is
         * evidence of no detected host, not proof that the cable is absent.
         * Keep MSC running so a later host can enumerate current contents. */
        ESP_LOGI(TAG, "USB ready locally; no host detected within %d ms", HOST_CONFIG_TIMEOUT_MS);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "USB host detected but MSC configuration timed out");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t install_usb(void)
{
    if (driver_installed || driver_uncertain || tud_inited()) return ESP_ERR_INVALID_STATE;
    usb_config = (tinyusb_config_t)TINYUSB_DEFAULT_CONFIG();
    usb_config.descriptor.device = &device_descriptor;
    usb_config.descriptor.full_speed_config = configuration_descriptor;
    usb_config.descriptor.string = strings;
    usb_config.descriptor.string_count = sizeof(strings) / sizeof(strings[0]);
    usb_config.event_cb = usb_event;
    xSemaphoreTake(host_changed, 0);
    atomic_store(&host_configured, false);
    driver_uncertain = true;
    esp_err_t err = tinyusb_driver_install(&usb_config);
    if (err == ESP_OK) {
        driver_installed = true;
        driver_uncertain = false;
        return wait_host_configuration();
    }
    return err;
}

static void disconnect_device(void *arg)
{
    (void)arg;
    disconnected = tud_disconnect();
    atomic_store(&host_configured, false);
    xSemaphoreGive(disconnect_done);
    /* This callback is reached between MSC operations with no storage mutex
     * held. Never return to the event loop: queued transfer completions could
     * otherwise start another SD read before the application deletes us.
     * An acknowledgement may wake the other core before this suspend; that
     * is safe because this non-returning tail cannot acquire any MSC lock. */
    for (;;) vTaskSuspend(NULL);
}

static void queue_disconnect(void *arg)
{
    (void)arg;
    /* This enqueue can block on a full TinyUSB queue. The caller bounds it by
     * owning and deleting this helper task after the completion wait. */
    usbd_defer_func(disconnect_device, NULL, false);
    vTaskSuspend(NULL);
}

static esp_err_t disconnect_usb(void)
{
    if (task_quiesced) return disconnected ? ESP_OK : ESP_FAIL;
    /* A previous bounded wait may have expired just before the callback
     * parked. Consume that acknowledgement rather than discard it. */
    if (xSemaphoreTake(disconnect_done, 0) == pdTRUE) {
        task_quiesced = true;
        vTaskDelay(pdMS_TO_TICKS(HOST_DETACH_MS));
        return disconnected ? ESP_OK : ESP_FAIL;
    }
    TaskHandle_t enqueue_task = NULL;
    if (xTaskCreate(queue_disconnect, "msc_detach", 2048, NULL, 5, &enqueue_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t done = xSemaphoreTake(disconnect_done, pdMS_TO_TICKS(DISCONNECT_TIMEOUT_MS));
    vTaskDelete(enqueue_task);
    if (done != pdTRUE) return ESP_ERR_TIMEOUT;
    task_quiesced = true;
    if (!disconnected) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(HOST_DETACH_MS));
    return ESP_OK;
}

static esp_err_t uninstall_usb(void)
{
    /* Never force-delete a live task after a timeout: it may own MSC's mutex.
     * Recovery must obtain the same quiescence acknowledgement or fail closed. */
    if ((driver_installed || tud_inited()) && !task_quiesced) return ESP_ERR_INVALID_STATE;
    esp_err_t err = tinyusb_driver_uninstall();
    if (err == ESP_OK || (!driver_installed && err == ESP_ERR_INVALID_STATE && !tud_inited())) {
        driver_installed = false;
        driver_uncertain = false;
        task_quiesced = false;
        disconnected = false;
        atomic_store(&host_configured, false);
        return ESP_OK;
    }
    driver_uncertain = true;
    return err;
}

/* The 2.3.0 setter overwrites its owner even when unmount fails. Recovery
 * therefore also removes any residual FATFS registration with USB stopped.
 * This never mounts or formats a filesystem. */
static esp_err_t remove_app_mount(void)
{
    BYTE pdrv = ff_diskio_get_pdrv_card(&card);
    if (pdrv != 0xff) {
        char drive[] = {(char)('0' + pdrv), ':', 0};
        if (f_mount(NULL, drive, 0) != FR_OK) return ESP_FAIL;
        ff_diskio_unregister(pdrv);
    }
    esp_err_t err = esp_vfs_fat_unregister_path("/sdcard");
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return app_mounted() ? ESP_FAIL : ESP_OK;
}

esp_err_t usb_storage_start_app(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    started = true;
    atomic_store(&usb_writable, false);
    mount_done = xSemaphoreCreateBinary();
    disconnect_done = xSemaphoreCreateBinary();
    host_changed = xSemaphoreCreateBinary();
    if (!mount_done || !disconnect_done || !host_changed) return ESP_ERR_NO_MEM;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_14;
    slot.cmd = GPIO_NUM_15;
    slot.d0 = GPIO_NUM_16;
    slot.d1 = GPIO_NUM_18;
    slot.d2 = GPIO_NUM_17;
    slot.d3 = GPIO_NUM_21;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) return err;
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) return err;
    err = sdmmc_card_init(&host, &card);
    if (err != ESP_OK) return err;
    sdmmc_card_print_info(stdout, &card);

    tinyusb_msc_driver_config_t msc = {
        .user_flags.auto_mount_off = 1,
        .callback = storage_event,
    };
    err = tinyusb_msc_install_driver(&msc);
    if (err != ESP_OK) return err;
    tinyusb_msc_storage_config_t config = {
        .medium.card = &card,
        .fat_fs = {
            .base_path = "/sdcard",
            .config = {.format_if_mount_failed = false, .max_files = 10},
            .do_not_format = true,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
    };
    err = tinyusb_msc_new_storage_sdmmc(&config, &storage);
    if (err == ESP_OK) err = wait_mount(TINYUSB_MSC_STORAGE_MOUNT_APP);
    if (err == ESP_OK) ownership.state = STORAGE_APP;
    return err;
}

esp_err_t usb_storage_expose(void)
{
    if (!storage || !storage_handoff_begin_to_usb(&ownership)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = set_mount(TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (err == ESP_OK) atomic_store(&usb_writable, true);
    if (err == ESP_OK) err = install_usb();
    if (err != ESP_OK) atomic_store(&usb_writable, false);
    storage_handoff_complete_to_usb(&ownership, err == ESP_OK);
    if (err != ESP_OK) {
        if (!driver_installed && !driver_uncertain && !tud_inited()) remove_app_mount();
        ESP_LOGE(TAG, "APP to USB failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t usb_storage_acquire(void)
{
    if (!storage || !storage_handoff_begin_to_app(&ownership)) return ESP_ERR_INVALID_STATE;
    atomic_store(&usb_writable, false);
    esp_err_t err = disconnect_usb();
    if (err == ESP_OK) err = uninstall_usb();
    if (err == ESP_OK) err = set_mount(TINYUSB_MSC_STORAGE_MOUNT_APP);
    storage_handoff_complete_to_app(&ownership, err == ESP_OK);
    if (err != ESP_OK) ESP_LOGE(TAG, "USB to APP failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t usb_storage_restore_usb(void)
{
    if (!storage || recovery_failed || !storage_handoff_begin_usb_recovery(&ownership)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&usb_writable, false);
    esp_err_t err = ESP_OK;
    /* A detach timeout grants no permission to delete the USB task. It must
     * acknowledge the parked boundary before teardown or FATFS cleanup. */
    if (driver_installed || driver_uncertain || tud_inited()) {
        if (tud_inited()) err = disconnect_usb();
        if (err == ESP_OK) err = uninstall_usb();
    }
    if (err == ESP_OK) err = remove_app_mount();
    if (err == ESP_OK) {
        err = tinyusb_msc_set_storage_mount_point(storage, TINYUSB_MSC_STORAGE_MOUNT_USB);
        tinyusb_msc_mount_point_t point;
        if (err == ESP_OK) err = tinyusb_msc_get_storage_mount_point(storage, &point);
        if (err == ESP_OK && (point != TINYUSB_MSC_STORAGE_MOUNT_USB || app_mounted())) err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        atomic_store(&usb_writable, true);
        err = install_usb();
    }
    if (err != ESP_OK) atomic_store(&usb_writable, false);
    storage_handoff_complete_to_usb(&ownership, err == ESP_OK);
    recovery_failed = err != ESP_OK;
    if (recovery_failed) ESP_LOGE(TAG, "USB recovery failed; reset required: %s", esp_err_to_name(err));
    return err;
}

bool usb_storage_app_owned(void)
{
    return ownership.state == STORAGE_APP;
}

bool usb_storage_host_configured(void)
{
    return ownership.state == STORAGE_USB && atomic_load(&host_configured) && tud_mounted();
}

esp_err_t usb_storage_last_io_error(void)
{
    return atomic_load(&last_io_error);
}
