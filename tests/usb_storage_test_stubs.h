/* SDK boundaries for host execution of the production storage controller. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM };
const char *esp_err_to_name(esp_err_t err);
void test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)

typedef unsigned TickType_t;
typedef int BaseType_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xTaskCreate(void (*function)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskSuspend(TaskHandle_t task);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

struct stat { unsigned st_mode; };
#define S_ISDIR(mode) ((mode) == 1)
int stat(const char *path, struct stat *info);
typedef unsigned char BYTE;
#define FR_OK 0
int f_mount(void *fs, const char *drive, int immediate);
BYTE ff_diskio_get_pdrv_card(const void *card);
void ff_diskio_unregister(BYTE drive);
esp_err_t esp_vfs_fat_unregister_path(const char *path);

typedef struct { int unused; } sdmmc_card_t;
typedef struct { int slot; } sdmmc_host_t;
typedef struct { int width, clk, cmd, d0, d1, d2, d3, flags; } sdmmc_slot_config_t;
#define SDMMC_HOST_DEFAULT() {0}
#define SDMMC_SLOT_CONFIG_DEFAULT() {0}
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
enum { GPIO_NUM_14=14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_21=21 };
esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *config);
esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card);
void sdmmc_card_print_info(void *stream, const sdmmc_card_t *card);

typedef struct {
    unsigned bLength, bDescriptorType, bcdUSB, bMaxPacketSize0, idVendor, idProduct,
             bcdDevice, iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} tusb_desc_device_t;
#define TUSB_DESC_DEVICE 1
#define TUD_CONFIG_DESC_LEN 9
#define TUD_MSC_DESC_LEN 23
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_MSC_DESCRIPTOR(...) 0
typedef struct { int id; } tinyusb_event_t;
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED };
typedef struct {
    struct { const tusb_desc_device_t *device; const uint8_t *full_speed_config;
             const char **string; int string_count; } descriptor;
    void (*event_cb)(tinyusb_event_t *, void *);
    void *event_arg;
} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG() {0}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
esp_err_t tinyusb_driver_uninstall(void);
bool tud_inited(void);
bool tud_disconnect(void);
bool tud_connected(void);
bool tud_mounted(void);
void usbd_defer_func(void (*callback)(void *), void *arg, bool in_isr);

typedef void *tinyusb_msc_storage_handle_t;
typedef enum { TINYUSB_MSC_STORAGE_MOUNT_USB, TINYUSB_MSC_STORAGE_MOUNT_APP } tinyusb_msc_mount_point_t;
enum { TINYUSB_MSC_EVENT_MOUNT_START, TINYUSB_MSC_EVENT_MOUNT_COMPLETE, TINYUSB_MSC_EVENT_MOUNT_FAILED };
typedef struct { int id; tinyusb_msc_mount_point_t mount_point; } tinyusb_msc_event_t;
typedef struct {
    struct { int auto_mount_off; } user_flags;
    void (*callback)(tinyusb_msc_storage_handle_t, tinyusb_msc_event_t *, void *);
} tinyusb_msc_driver_config_t;
typedef struct {
    struct { sdmmc_card_t *card; } medium;
    struct { const char *base_path; struct { bool format_if_mount_failed; int max_files; } config;
             bool do_not_format; } fat_fs;
    tinyusb_msc_mount_point_t mount_point;
} tinyusb_msc_storage_config_t;
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config, tinyusb_msc_storage_handle_t *handle);
esp_err_t tinyusb_msc_set_storage_mount_point(tinyusb_msc_storage_handle_t storage, tinyusb_msc_mount_point_t point);
esp_err_t tinyusb_msc_get_storage_mount_point(tinyusb_msc_storage_handle_t storage, tinyusb_msc_mount_point_t *point);
