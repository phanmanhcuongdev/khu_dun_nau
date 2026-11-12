#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

/* Shared ADC1 one-shot owner for analog sensor channels. */
esp_err_t adc_shared_init_channel(adc_channel_t channel);
esp_err_t adc_shared_read(adc_channel_t channel, int *raw_out, int *mv_out,
                          bool *mv_valid_out);
