#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef int wifi_init_config_t;
typedef struct{struct{uint8_t ssid[32],password[64];struct{int authmode;}threshold;}sta;}wifi_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() 0
#define WIFI_AUTH_WPA2_PSK 2
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,const wifi_config_t*);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);

typedef struct{unsigned reason;}wifi_event_sta_disconnected_t;
