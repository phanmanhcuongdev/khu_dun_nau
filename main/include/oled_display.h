#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sensor_types.h"

esp_err_t oled_display_init(void);
void oled_display_update(const kitchen_sensor_data_t *data, bool wifi_connected, bool mqtt_connected);
