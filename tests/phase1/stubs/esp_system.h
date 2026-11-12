#pragma once
#include <stddef.h>
#define ESP_RST_POWERON 1
#define ESP_RST_SW 3
#define ESP_RST_BROWNOUT 9
int esp_reset_reason(void);
size_t esp_get_free_heap_size(void);
size_t esp_get_minimum_free_heap_size(void);
