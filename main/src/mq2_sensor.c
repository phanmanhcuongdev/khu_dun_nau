#include "mq2_sensor.h"
#include "board_pins.h"
#include "adc_shared.h"
#include "esp_log.h"

static const char *TAG = "MQ2";
static bool s_gpio_initialized = false;

esp_err_t mq2_sensor_init(void)
{
    if (s_gpio_initialized) {
        return ESP_OK;
    }

    if (MQ2_DO_PIN != GPIO_NUM_NC && MQ2_DO_PIN >= 0) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << MQ2_DO_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        esp_err_t gpio_err = gpio_config(&io_conf);
        if (gpio_err != ESP_OK) {
            ESP_LOGE(TAG, "Loi gpio_config MQ2_DO (pin %d): %d", MQ2_DO_PIN, gpio_err);
            return gpio_err;
        }
        s_gpio_initialized = true;

        esp_err_t adc_err = adc_shared_init_channel(MQ2_AO_PIN);
        if (adc_err != ESP_OK) {
            ESP_LOGE(TAG, "Loi khoi tao ADC1_CH%d cho MQ2 AO: %d", MQ2_AO_PIN, adc_err);
            return adc_err;
        }
        ESP_LOGI(TAG, "Khoi tao MQ-2 thanh cong (DO:%d, AO:ADC1_CH%d)", MQ2_DO_PIN, MQ2_AO_PIN);
    } else {
        s_gpio_initialized = true;
        ESP_LOGW(TAG, "MQ-2 DO chua gan chan (tam de GPIO_NUM_NC)");
    }
    return ESP_OK;
}

int mq2_sensor_read_digital(void)
{
    if (MQ2_DO_PIN != GPIO_NUM_NC && MQ2_DO_PIN >= 0) {
        return gpio_get_level(MQ2_DO_PIN);
    }
    return (MQ2_DO_ACTIVE_LEVEL == 0) ? 1 : 0;
}

esp_err_t mq2_sensor_read_raw(int *raw_out)
{
    /* Legacy API: preserve historical DO-derived raw semantics. */
    if (raw_out) {
        int level = (MQ2_DO_PIN != GPIO_NUM_NC && MQ2_DO_PIN >= 0) ? gpio_get_level(MQ2_DO_PIN) : 1;
        bool gas = (level == MQ2_DO_ACTIVE_LEVEL);
        *raw_out = gas ? 1000 : 200;
    }
    return ESP_OK;
}

esp_err_t mq2_sensor_read_analog(int *raw_out, int *mv_out, bool *mv_valid_out)
{
    if (!s_gpio_initialized) return ESP_ERR_INVALID_STATE;
    return adc_shared_read(MQ2_AO_PIN, raw_out, mv_out, mv_valid_out);
}

esp_err_t mq2_sensor_read(int *raw_out, int *do_out, bool *gas_detected, bool *valid, esp_err_t *err_code)
{
    if (!s_gpio_initialized) {
        if (valid) *valid = false;
        if (err_code) *err_code = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Đọc chân số DO
    int do_val = (MQ2_DO_ACTIVE_LEVEL == 0) ? 1 : 0; // Chế độ chưa nối chân: mặc định mức an toàn
    if (MQ2_DO_PIN != GPIO_NUM_NC && MQ2_DO_PIN >= 0) {
        do_val = gpio_get_level(MQ2_DO_PIN);
    }
    if (do_out) *do_out = do_val;
    bool is_detected = (do_val == MQ2_DO_ACTIVE_LEVEL);
    if (gas_detected) {
        *gas_detected = is_detected;
    }

    // Legacy telemetry/risk input remains DO-derived until analog rules are explicitly approved.
    if (raw_out) *raw_out = is_detected ? 1000 : 200;

    if (valid) *valid = true;
    if (err_code) *err_code = ESP_OK;
    return ESP_OK;
}
