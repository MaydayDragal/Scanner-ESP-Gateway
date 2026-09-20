#pragma once
#define CFG_TUSB_MCU OPT_MCU_NONE
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MSC 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_MSC_EP_BUFSIZE 512
#define CFG_TUSB_DEBUG 0
#define SOC_SDMMC_HOST_SUPPORTED 1
#define CONFIG_TINYUSB_MSC_BUFSIZE 512
#define CONFIG_TINYUSB_MSC_MOUNT_PATH "/sdcard"
#define CONFIG_WL_SECTOR_SIZE 512

#define TUP_DCD_ENDPOINT_MAX 8
#define WL_INVALID_HANDLE (-1)
/* Baseline upstream SPIFlash overflow checks assume 32-bit size_t. */
#define __builtin_umul_overflow __builtin_mul_overflow
#define __builtin_uadd_overflow __builtin_add_overflow
