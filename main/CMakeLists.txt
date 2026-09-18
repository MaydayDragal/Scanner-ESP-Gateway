idf_component_register(SRCS "main.c" "scanner_wifi.c" "scanner_capture.c" "esci_scan.c" INCLUDE_DIRS "." PRIV_REQUIRES fatfs esp_wifi esp_netif esp_event nvs_flash esp_timer)
