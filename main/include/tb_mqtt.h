#pragma once
#include "event_buffer.h"
#define TB_BROKER_URI "mqtts://iot.cuongdso.id.vn:8883"
#define NODE_ID "ESP32_S3_MINI_APT_01"
#define ZONE_ID "V1"
esp_err_t tb_mqtt_init(void);
bool tb_mqtt_is_connected(void);
esp_err_t tb_send_telemetry_async(const kitchen_sensor_data_t *data);
esp_err_t tb_send_telemetry(const kitchen_sensor_data_t *data);
bool tb_time_is_synced(void);
int64_t tb_get_current_timestamp(void);

void tb_safety_timing(int64_t step_us,int64_t interval_us);
