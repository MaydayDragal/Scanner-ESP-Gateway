/* SDK boundaries for host execution of the production storage controller. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define IRAM_ATTR

typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND, ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_SIZE };
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

typedef struct { struct { unsigned capacity,sector_size; } csd; } sdmmc_card_t;
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

#include "tusb.h"
#include "device/usbd_pvt.h"
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

/* Use the real component event/config ABI, with only SDK boundaries stubbed. */
typedef int wl_handle_t;
typedef struct { bool format_if_mount_failed; int max_files; } esp_vfs_fat_mount_config_t;
#include "tinyusb_msc.h"
#include "msc_storage.h"
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#define ESP_LOGD(...) test_log(__VA_ARGS__)
#define ESP_RETURN_ON_FALSE(c,e,...) do { if (!(c)) return (e); } while(0)
#define ESP_RETURN_ON_ERROR(e,...) do { esp_err_t rc=(e); if(rc!=ESP_OK) return rc; } while(0)
#define ESP_GOTO_ON_ERROR(e,l,...) do { ret=(e); if(ret!=ESP_OK) goto l; } while(0)
#define ESP_ERROR_CHECK(e) assert((e)==ESP_OK)
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5,5,5)
#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_DEFAULT 0
#define heap_caps_calloc(n,s,c) calloc(n,s)
#define heap_caps_aligned_calloc(a,n,s,c) calloc(n,s)
#define heap_caps_free free
#define portMAX_DELAY ((TickType_t)-1)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t sem);
typedef int FATFS;
typedef int FRESULT;
typedef struct { int fmt,n_fat,align,n_root; size_t au_size; } MKFS_PARM;
enum { FR_NO_FILESYSTEM=1, FR_INT_ERR, FM_ANY };
#define ff_memalloc malloc
#define ff_memfree free
int f_mkfs(const char *,const MKFS_PARM *,void *,size_t);
esp_err_t ff_diskio_get_drive(BYTE *drive);
size_t esp_vfs_fat_get_allocation_unit_size(size_t sector,size_t work);
typedef struct { const char *base_path,*fat_drive; int max_files; } esp_vfs_fat_conf_t;
esp_err_t esp_vfs_fat_register_cfg(const esp_vfs_fat_conf_t *config,FATFS **fs);

void ff_diskio_register_sdmmc(BYTE drive,sdmmc_card_t *card);
void ff_sdmmc_set_disk_status_check(BYTE drive,bool check);
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card,void *dest,size_t start,size_t count);
esp_err_t sdmmc_write_sectors(sdmmc_card_t *card,const void *src,size_t start,size_t count);
