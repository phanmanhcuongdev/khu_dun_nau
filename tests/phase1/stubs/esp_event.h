#pragma once
#include "esp_err.h"
#include "mqtt_client.h"
#include <stdint.h>
typedef int esp_event_handler_instance_t;
extern const char *WIFI_EVENT,*IP_EVENT;
#define IP_EVENT_STA_GOT_IP 1
#define WIFI_EVENT_STA_START 2
#define WIFI_EVENT_STA_DISCONNECTED 3
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int32_t,void(*)(void*,esp_event_base_t,int32_t,void*),void*,esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t,int32_t,esp_event_handler_instance_t);
