#include "scanner_wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include "nvs_flash.h"

#if __has_include("scanner_wifi_local.h")
#include "scanner_wifi_local.h"
#define SCANNER_WIFI_CONFIGURED 1
#else
#define SCANNER_WIFI_CONFIGURED 0
#endif

static const char *TAG = "scanner_wifi";

#if SCANNER_WIFI_CONFIGURED
static EventGroupHandle_t wifi_events;
static esp_netif_t *wifi_netif;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1
static bool reconnect_scanner;

static void station_config(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
}

static bool connected_to_scanner(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
           strcmp((const char *)ap.ssid, SCANNER_WIFI_SSID) == 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_events, WIFI_DISCONNECTED_BIT);
        if (reconnect_scanner) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(wifi_events, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void synchronize_clock(void)
{
#if defined(TIME_WIFI_SSID) && defined(TIME_WIFI_PASSWORD)
    station_config(TIME_WIFI_SSID, TIME_WIFI_PASSWORD);
    ESP_LOGI(TAG, "connecting to home Wi-Fi for time sync");
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                          pdMS_TO_TICKS(12000));
    if (bits & WIFI_CONNECTED_BIT) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&config) == ESP_OK) {
            esp_err_t synced = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
            time_t now = time(NULL);
            ESP_LOGI(TAG, "Internet time sync: %s", synced == ESP_OK && now > 1704067200 ? "ready" : "failed");
            esp_netif_sntp_deinit();
        } else {
            ESP_LOGW(TAG, "could not start Internet time sync");
        }
    } else {
        ESP_LOGW(TAG, "home Wi-Fi time sync connection timed out");
    }
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT);
    esp_wifi_disconnect();
    xEventGroupWaitBits(wifi_events, WIFI_DISCONNECTED_BIT, pdFALSE, pdFALSE,
                        pdMS_TO_TICKS(1000));
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT);
#else
    ESP_LOGW(TAG, "home Wi-Fi time sync not configured");
#endif
}

int scanner_wifi_open_connection(uint32_t gateway_ip)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_in endpoint = {
        .sin_family = AF_INET,
        .sin_port = htons(1865),
        .sin_addr.s_addr = gateway_ip,
    };
    int rc = connect(fd, (struct sockaddr *)&endpoint, sizeof(endpoint));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    if (rc < 0) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        struct timeval timeout = { .tv_sec = 3 };
        rc = select(fd + 1, NULL, &writable, NULL, &timeout);
        int error = 0;
        socklen_t length = sizeof(error);
        if (rc <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error != 0) {
            close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL, flags);
    return fd;
}
#else
int scanner_wifi_open_connection(uint32_t gateway_ip)
{
    (void)gateway_ip;
    return -1;
}
#endif

scanner_wifi_result_t scanner_wifi_current(void)
{
    scanner_wifi_result_t result={0};
#if SCANNER_WIFI_CONFIGURED
    result.configured=true;
    if(wifi_events && wifi_netif && (xEventGroupGetBits(wifi_events)&WIFI_CONNECTED_BIT) &&
       connected_to_scanner()) {
        esp_netif_ip_info_t info;
        if(esp_netif_get_ip_info(wifi_netif,&info)==ESP_OK) {
            result.gateway_ip=info.gw.addr;
            result.connected=result.gateway_ip!=0;
        }
    }
#endif
    return result;
}

scanner_wifi_result_t scanner_wifi_start(void)
{
    scanner_wifi_result_t result = {0};
#if !SCANNER_WIFI_CONFIGURED
    ESP_LOGW(TAG, "scanner_wifi_local.h missing; scanner Wi-Fi is not configured");
    return result;
#else
    result.configured = true;
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return result;
    }
    wifi_netif = esp_netif_create_default_wifi_sta();
    if (!wifi_netif) {
        ESP_LOGE(TAG, "Wi-Fi netif creation failed");
        return result;
    }
    wifi_events = xEventGroupCreate();
    if (!wifi_events) {
        ESP_LOGE(TAG, "Wi-Fi event group creation failed");
        return result;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    synchronize_clock();
    station_config(SCANNER_WIFI_SSID, SCANNER_WIFI_PASSWORD);
    reconnect_scanner = true;
    ESP_LOGI(TAG, "connecting to scanner Wi-Fi Direct network");
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                          pdMS_TO_TICKS(30000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "scanner Wi-Fi connection timed out");
        return result;
    }
    if (!connected_to_scanner()) {
        ESP_LOGW(TAG, "Wi-Fi connected to unexpected network");
        return result;
    }
    result.connected = true;
    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(wifi_netif, &ip_info));
    result.gateway_ip = ip_info.gw.addr;
    ESP_LOGI(TAG, "scanner gateway: " IPSTR, IP2STR(&ip_info.gw));
    if (result.gateway_ip == 0) {
        return result;
    }
    return result;
#endif
}
