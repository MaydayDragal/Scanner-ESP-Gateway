#include "scanner_clock_model.h"

#include <assert.h>
#include <stdint.h>

#ifndef SCANNER_CLOCK_WIFI_BOUNDARY_TEST
int main(void)
{
    scanner_clock_state_t state = scanner_clock_current();
    assert(!state.valid);
    assert(state.source == SCANNER_CLOCK_UNKNOWN);
    assert(state.last_sync_monotonic_us == 0);
    assert(state.last_error == SCANNER_CLOCK_ERROR_NONE);

    scanner_clock_record_sync_failure(SCANNER_CLOCK_ERROR_TIME_WIFI_NOT_CONFIGURED);
    state = scanner_clock_current();
    assert(!state.valid);
    assert(state.source == SCANNER_CLOCK_UNKNOWN);
    assert(state.last_error == SCANNER_CLOCK_ERROR_TIME_WIFI_NOT_CONFIGURED);

    scanner_clock_record_sync_failure(SCANNER_CLOCK_ERROR_HOME_AP_TIMEOUT);
    state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_HOME_AP_TIMEOUT);

    scanner_clock_record_sync_failure(SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);
    state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);

    const int64_t first_sync_us = INT64_C(41234567);
    scanner_clock_record_ntp_success(first_sync_us);
    state = scanner_clock_current();
    assert(state.valid);
    assert(state.source == SCANNER_CLOCK_NTP);
    assert(state.last_sync_monotonic_us == first_sync_us);
    assert(state.last_error == SCANNER_CLOCK_ERROR_NONE);

    scanner_clock_record_sync_failure(SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);
    state = scanner_clock_current();
    assert(state.valid);
    assert(state.source == SCANNER_CLOCK_NTP);
    assert(state.last_sync_monotonic_us == first_sync_us);
    assert(state.last_error == SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);

    const int64_t retry_sync_us = INT64_C(98765432);
    scanner_clock_record_ntp_success(retry_sync_us);
    state = scanner_clock_current();
    assert(state.valid);
    assert(state.source == SCANNER_CLOCK_NTP);
    assert(state.last_sync_monotonic_us == retry_sync_us);
    assert(state.last_error == SCANNER_CLOCK_ERROR_NONE);
    return 0;
}
#else

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* SDK boundary doubles: the production Wi-Fi service and clock model stay real. */
#define ESP_OK 0
#define ESP_FAIL -1
#define BIT0 1U
#define BIT1 2U
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define ESP_EVENT_ANY_ID -1
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_PS_NONE 0
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(host) ((esp_sntp_config_t){host})
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 0U, 0U, 0U, 0U
#define SCANNER_WIFI_NATIVE_TEST 1
#define SCANNER_WIFI_SSID "test-scanner"
#define SCANNER_WIFI_PASSWORD "test-password"
#ifdef SCANNER_CLOCK_TEST_WITH_TIME_CREDENTIALS
#define TIME_WIFI_SSID "test-home"
#define TIME_WIFI_PASSWORD "test-home-password"
#endif

typedef int esp_err_t;
typedef int esp_event_base_t;
typedef unsigned EventBits_t;
typedef struct { EventBits_t bits; } test_event_group_t;
typedef test_event_group_t *EventGroupHandle_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { char ssid[32]; } wifi_ap_record_t;
typedef struct { struct { char ssid[32]; char password[64]; } sta; } wifi_config_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct { const char *server; } esp_sntp_config_t;
typedef struct { uint32_t addr; } test_ip_addr_t;
typedef struct { test_ip_addr_t gw; } esp_netif_ip_info_t;

static test_event_group_t test_events;
static esp_netif_t test_netif;
static unsigned connect_count;
static bool home_ap_available;
static bool sntp_init_ok;
static bool sntp_wait_ok;
static time_t observed_wall_time;
static int64_t observed_monotonic_us;
static char configured_ssid[32];

static void test_log(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }

static size_t test_strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dst, src, copied);
        dst[copied] = '\0';
    }
    return length;
}

static int test_setenv(const char *name, const char *value, int overwrite)
{ (void)name; (void)value; (void)overwrite; return 0; }
static void test_tzset(void) {}
#ifdef SCANNER_CLOCK_TEST_WITH_TIME_CREDENTIALS
static time_t test_time(time_t *result)
{ if (result) *result = observed_wall_time; return observed_wall_time; }
#endif

#define strlcpy test_strlcpy
#define setenv test_setenv
#define tzset test_tzset
#define time test_time

static esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config)
{ (void)interface; test_strlcpy(configured_ssid, config->sta.ssid, sizeof(configured_ssid)); return ESP_OK; }
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{ test_strlcpy(ap->ssid, configured_ssid, sizeof(ap->ssid)); return ESP_OK; }
static esp_err_t esp_wifi_connect(void)
{
    ++connect_count;
#ifdef SCANNER_CLOCK_TEST_WITH_TIME_CREDENTIALS
    test_events.bits = connect_count % 2 ? (home_ap_available ? BIT0 : 0) : BIT0;
#else
    test_events.bits = BIT0;
#endif
    return ESP_OK;
}
#ifdef SCANNER_CLOCK_TEST_WITH_TIME_CREDENTIALS
static esp_err_t esp_wifi_disconnect(void)
{ test_events.bits = BIT1; return ESP_OK; }
#endif
static EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
                                      int clear_on_exit, int wait_all, int timeout)
{ (void)clear_on_exit; (void)wait_all; (void)timeout; return group->bits & bits; }
static void xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits)
{ group->bits &= ~bits; }
static void xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{ group->bits |= bits; }
static EventBits_t xEventGroupGetBits(EventGroupHandle_t group)
{ return group->bits; }
static EventGroupHandle_t xEventGroupCreate(void)
{ test_events.bits = 0; return &test_events; }
#ifdef SCANNER_CLOCK_TEST_WITH_TIME_CREDENTIALS
static esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{ (void)config; return sntp_init_ok ? ESP_OK : ESP_FAIL; }
static esp_err_t esp_netif_sntp_sync_wait(int timeout)
{ (void)timeout; return sntp_wait_ok ? ESP_OK : ESP_FAIL; }
static void esp_netif_sntp_deinit(void) {}
static int64_t esp_timer_get_time(void)
{ return observed_monotonic_us; }
#endif
static esp_err_t nvs_flash_init(void) { return ESP_OK; }
static esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &test_netif; }
static esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{ (void)config; return ESP_OK; }
static esp_err_t esp_event_handler_register(esp_event_base_t base, int id,
                                            void (*handler)(void *, esp_event_base_t, int32_t, void *), void *arg)
{ (void)base; (void)id; (void)handler; (void)arg; return ESP_OK; }
static esp_err_t esp_wifi_set_storage(int storage) { (void)storage; return ESP_OK; }
static esp_err_t esp_wifi_set_mode(int mode) { (void)mode; return ESP_OK; }
static esp_err_t esp_wifi_start(void) { return ESP_OK; }
static esp_err_t esp_wifi_set_ps(int ps) { (void)ps; return ESP_OK; }
static esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{ (void)netif; info->gw.addr = 1; return ESP_OK; }
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "error"; }

#include "../main/scanner_wifi.c"

static void start_with(bool ap, bool init, bool sync, time_t wall, int64_t monotonic)
{
    connect_count = 0;
    home_ap_available = ap;
    sntp_init_ok = init;
    sntp_wait_ok = sync;
    observed_wall_time = wall;
    observed_monotonic_us = monotonic;
    scanner_wifi_result_t result = scanner_wifi_start();
    assert(result.configured && result.connected);
    assert(result.gateway_ip == 1);
}

int main(void)
{
    const time_t recent_wall_time = (time_t)1780000000;
#ifndef SCANNER_CLOCK_TEST_WITH_TIME_CREDENTIALS
    start_with(false, false, false, recent_wall_time, 111);
    scanner_clock_state_t state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_TIME_WIFI_NOT_CONFIGURED);
    assert(connect_count == 1);
#else
    start_with(false, true, true, recent_wall_time, 111);
    scanner_clock_state_t state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_HOME_AP_TIMEOUT);

    start_with(true, false, true, recent_wall_time, 123);
    state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_SNTP_INIT);

    start_with(true, true, false, recent_wall_time, 222);
    state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);

    start_with(true, true, true, (time_t)1700000000, 232);
    state = scanner_clock_current();
    assert(!state.valid);
    assert(state.last_error == SCANNER_CLOCK_ERROR_INVALID_TIME);

    start_with(true, true, true, recent_wall_time, 333);
    state = scanner_clock_current();
    assert(state.valid && state.source == SCANNER_CLOCK_NTP);
    assert(state.last_sync_monotonic_us == 333);
    assert(state.last_error == SCANNER_CLOCK_ERROR_NONE);

    start_with(true, true, false, recent_wall_time, 444);
    state = scanner_clock_current();
    assert(state.valid);
    assert(state.last_sync_monotonic_us == 333);
    assert(state.last_error == SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);
#endif
    return 0;
}
#endif
