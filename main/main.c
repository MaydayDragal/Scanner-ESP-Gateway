#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "scanner_gateway";
static sdmmc_card_t card;
static tinyusb_msc_storage_handle_t storage;

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

    tinyusb_msc_storage_config_t storage_config = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .medium.card = &card,
        .fat_fs = {
            .base_path = NULL,
            .config.max_files = 5,
            .format_flags = 0,
        },
    };
    ESP_ERROR_CHECK(tinyusb_msc_new_storage_sdmmc(&storage_config, &storage));

    tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    usb_config.descriptor.device = &device_descriptor;
    usb_config.descriptor.full_speed_config = configuration_descriptor;
    usb_config.descriptor.string = string_descriptors;
    usb_config.descriptor.string_count = sizeof(string_descriptors) / sizeof(string_descriptors[0]);
    ESP_ERROR_CHECK(tinyusb_driver_install(&usb_config));
    ESP_LOGI(TAG, "microSD available over USB mass storage");
}
