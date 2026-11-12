#pragma once
#include "driver/gpio.h"
static int GPIO;
static inline int gpio_ll_get_level(void *hw,int pin){(void)hw;return gpio_get_level(pin);}
