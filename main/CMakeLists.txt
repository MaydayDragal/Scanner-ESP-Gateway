idf_component_register(SRCS "main.c" "scanner_wifi.c" INCLUDE_DIRS "." PRIV_REQUIRES fatfs esp_wifi esp_netif esp_event nvs_flash)
