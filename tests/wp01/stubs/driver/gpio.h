#pragma once
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#define GPIO_INTR_ANYEDGE 3
#define GPIO_MODE_INPUT 1
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
typedef struct {int intr_type,mode;uint64_t pin_bit_mask;int pull_down_en,pull_up_en;} gpio_config_t;
int gpio_get_level(int);
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_install_isr_service(int);
esp_err_t gpio_isr_handler_add(int,void(*)(void*),void*);
esp_err_t gpio_isr_handler_remove(int);
esp_err_t gpio_intr_disable(int);
esp_err_t gpio_intr_enable(int);
