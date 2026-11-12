#pragma once
#include "esp_err.h"
typedef int esp_netif_t;
typedef int esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() 0
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_new(const esp_netif_config_t*);
void esp_netif_destroy_default_wifi(void*);
