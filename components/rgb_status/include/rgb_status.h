#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { RGB_BOOTING, RGB_INFO, RGB_WARNING, RGB_FIRE, RGB_SENSOR_ERROR, RGB_OFF, RGB_MANUAL } rgb_status_t;
/* Lifecycle: normal task only, serialized, with runtime callers quiesced.
 * Runtime setters are thread safe.
 * Owns low-speed timer 3, channels 5/6/7 exclusively while initialized. */
esp_err_t rgb_status_init(int r, int g, int b);
void rgb_status_set(rgb_status_t state);
void rgb_status_color(uint8_t r, uint8_t g, uint8_t b);
void rgb_status_off(void);
void rgb_status_deinit(void);
