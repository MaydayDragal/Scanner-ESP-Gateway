#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "lwip/inet.h"
#include "sdmmc_cmd.h"
#include "scanner_wifi.h"
#include "scanner_capture.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "scanner_gateway";
static sdmmc_card_t card;
static tinyusb_msc_storage_handle_t storage;
static bool storage_unmounted;

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return false;
}

static void storage_event(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    (void)handle; (void)arg;
    if(event->id==TINYUSB_MSC_EVENT_MOUNT_COMPLETE)
        storage_unmounted=event->mount_point==TINYUSB_MSC_STORAGE_MOUNT_USB;
}

#define MSC_DESCRIPTOR_LENGTH (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, /* Development prototype; replace before distribution. */
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, MSC_DESCRIPTOR_LENGTH,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
};

static const char *string_descriptors[] = {
    (const char[]) {0x09, 0x04},
    "Scanner ESP Gateway",
    "Scanner ESP microSD",
    "000001",
};

static esp_err_t init_card(void)
{
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

    ESP_RETURN_ON_ERROR(sdmmc_host_init(), TAG, "SDMMC host init failed");
    esp_err_t result = sdmmc_host_init_slot(host.slot, &slot);
    if (result == ESP_OK) {
        result = sdmmc_card_init(&host, &card);
    }
    if (result != ESP_OK) {
        sdmmc_host_deinit();
    }
    return result;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_card());
    sdmmc_card_print_info(stdout, &card);
    tinyusb_msc_driver_config_t msc_driver={
        .user_flags.auto_mount_off=1,
        .callback=storage_event,
    };
    ESP_ERROR_CHECK(tinyusb_msc_install_driver(&msc_driver));

    tinyusb_msc_storage_config_t storage_config = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
        .medium.card = &card,
        .fat_fs = {
            .base_path = "/sdcard",
            .config.max_files = 5,
            .do_not_format = true,
            .format_flags = 0,
        },
    };
    ESP_ERROR_CHECK(tinyusb_msc_new_storage_sdmmc(&storage_config, &storage));

    scanner_wifi_result_t scanner = scanner_wifi_start();
    scanner_capture_result_t capture={.scan.message="Scan skipped: scanner unavailable"};
    if(scanner.connected && scanner.gateway_ip) {
        ESP_LOGI(TAG,"Starting one 600 dpi color scan to SD");
        capture=scanner_capture(scanner.gateway_ip);
        ESP_LOGI(TAG,"%s (%" PRIu32 " bytes)",capture.scan.message,capture.scan.bytes);
    }
    FILE *status = fopen("/sdcard/GATEWAY.TXT", "w");
    if (status) {
        ip4_addr_t gateway = { .addr = scanner.gateway_ip };
        fprintf(status, "Scanner ESP Gateway\n");
        fprintf(status, "Wi-Fi configured: %s\n", scanner.configured ? "yes" : "no");
        fprintf(status, "Wi-Fi connected: %s\n", scanner.connected ? "yes" : "no");
        fprintf(status, "Reset reason: %d\n",(int)esp_reset_reason());
        fprintf(status, "Scanner TCP/1865: %s\n", capture.port_open ? "open" : "unavailable");
        fprintf(status, "Scan: %s\n",capture.scan.message);
        fprintf(status, "Scan complete: %s\n",capture.scan.complete?"yes":"no");
        fprintf(status, "Scanner released: %s\n",capture.scan.released?"yes":"no");
        fprintf(status, "Scan file: %s\n",capture.filename);
        fprintf(status, "Scan bytes: %" PRIu32 "\n",capture.scan.bytes);
        fprintf(status, "Quality: 600 dpi RGB, scanner JPEG quality 100\n");
        if (scanner.gateway_ip) {
            fprintf(status, "Scanner gateway: %s\n", ip4addr_ntoa(&gateway));
        }
        fclose(status);
    } else {
        ESP_LOGE(TAG, "could not write /sdcard/GATEWAY.TXT");
    }
    ESP_ERROR_CHECK(tinyusb_msc_set_storage_mount_point(storage, TINYUSB_MSC_STORAGE_MOUNT_USB));
    ESP_ERROR_CHECK(storage_unmounted?ESP_OK:ESP_FAIL);

    tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    usb_config.descriptor.device = &device_descriptor;
    usb_config.descriptor.full_speed_config = configuration_descriptor;
    usb_config.descriptor.string = string_descriptors;
    usb_config.descriptor.string_count = sizeof(string_descriptors) / sizeof(string_descriptors[0]);
    ESP_ERROR_CHECK(tinyusb_driver_install(&usb_config));
    ESP_LOGI(TAG, "microSD available over USB mass storage");
}
