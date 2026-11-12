#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef struct fake_mqtt *esp_mqtt_client_handle_t;
typedef struct {int msg_id;} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;
enum {MQTT_EVENT_CONNECTED=1,MQTT_EVENT_DISCONNECTED,MQTT_EVENT_PUBLISHED,MQTT_EVENT_DELETED,MQTT_EVENT_ERROR};
#define ESP_EVENT_ANY_ID -1
typedef struct {
 struct {struct{const char *uri;}address;struct{const char *certificate;}verification;}broker;
 struct{const char *username;}credentials;
 struct{bool disable_auto_reconnect;int timeout_ms;}network;
 struct{int limit;}outbox;
 struct{int keepalive;bool disable_clean_session;}session;
 struct{int priority,stack_size;}task;
} esp_mqtt_client_config_t;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t,int,void(*)(void*,esp_event_base_t,int32_t,void*),void*);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t,const char*,const char*,int,int,int,bool);
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t);
