#pragma once
#include "sensor_types.h"
#define OLED_UI_BYTES 1024
/* Pure presentation model, no device or risk-engine mutation. */
typedef struct {
    char title[17], detail[17], action[17], footer[17];
    bool temperature_large;
} oled_ui_view_t;
void oled_ui_compose(const kitchen_sensor_data_t *data, bool wifi, bool mqtt,
                     int64_t now_us, oled_ui_view_t *view);
/* Returns false for text/font bounds violations; never silently clips. */
bool oled_ui_render(const oled_ui_view_t *view, bool heartbeat, uint8_t frame[OLED_UI_BYTES]);
