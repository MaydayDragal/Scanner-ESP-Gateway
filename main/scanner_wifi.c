#include "scanner_wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
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

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static int connect_to_scanner(uint32_t gateway_ip)
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
#endif

scanner_wifi_result_t scanner_wifi_start_and_probe(void)
{
    scanner_wifi_result_t result = {0};
#if !SCANNER_WIFI_CONFIGURED
    ESP_LOGW(TAG, "scanner_wifi_local.h missing; scanner Wi-Fi is not configured");
    return result;
#else
    result.configured = true;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return result;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
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
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, SCANNER_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, SCANNER_WIFI_PASSWORD, sizeof(config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to scanner Wi-Fi Direct network");
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                          pdMS_TO_TICKS(30000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "scanner Wi-Fi connection timed out");
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
    int fd = connect_to_scanner(result.gateway_ip);
    if (fd < 0) {
        ESP_LOGW(TAG, "scanner TCP/1865 did not answer");
        return result;
    }
    result.scanner_port_open = true;
    struct timeval timeout = { .tv_sec = 2 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ssize_t count = recv(fd, result.welcome, sizeof(result.welcome), 0);
    if (count > 0) {
        result.welcome_length = (size_t)count;
    }
    close(fd);
    ESP_LOGI(TAG, "scanner TCP/1865 answered");
    return result;
#endif
}
