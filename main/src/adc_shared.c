#include "adc_shared.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static adc_oneshot_unit_handle_t s_adc1_handle;
static adc_cali_handle_t s_cali_handle;
static bool s_cali_available;
static uint32_t s_configured_channels;

static void adc_shared_try_calibration(adc_channel_t channel)
{
    if (s_cali_handle != NULL || s_cali_available) return;

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
        s_cali_available = true;
    }
}

esp_err_t adc_shared_init_channel(adc_channel_t channel)
{
    if (s_adc1_handle == NULL) {
        adc_oneshot_unit_init_cfg_t unit_config = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc1_handle);
        if (err != ESP_OK) return err;
        adc_shared_try_calibration(channel);
    }

    uint32_t bit = 1UL << channel;
    if ((s_configured_channels & bit) == 0) {
        adc_oneshot_chan_cfg_t channel_config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        esp_err_t err = adc_oneshot_config_channel(s_adc1_handle, channel, &channel_config);
        if (err != ESP_OK) return err;
        s_configured_channels |= bit;
    }
    return ESP_OK;
}

esp_err_t adc_shared_read(adc_channel_t channel, int *raw_out, int *mv_out,
                          bool *mv_valid_out)
{
    if (s_adc1_handle == NULL || (s_configured_channels & (1UL << channel)) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc1_handle, channel, &raw);
    if (err != ESP_OK) return err;
    if (raw_out) *raw_out = raw;

    bool mv_valid = false;
    if (mv_out) *mv_out = -1;
    if (s_cali_available) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK) {
            if (mv_out) *mv_out = mv;
            mv_valid = true;
        }
    }
    if (mv_valid_out) *mv_valid_out = mv_valid;
    return ESP_OK;
}
