#include "flame_sensor.h"
#include "board_pins.h"
#include "adc_shared.h"
#include "esp_log.h"

static const char *TAG = "FLAME";
static bool s_gpio_initialized = false;

esp_err_t flame_sensor_init(void)
{
    if (s_gpio_initialized) {
        return ESP_OK;
    }

    if (FLAME_DO_PIN != GPIO_NUM_NC && FLAME_DO_PIN >= 0) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << FLAME_DO_PIN),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        esp_err_t gpio_err = gpio_config(&io_conf);
        if (gpio_err != ESP_OK) {
            ESP_LOGE(TAG, "Loi gpio_config FLAME_DO (pin %d): %d", FLAME_DO_PIN, gpio_err);
            return gpio_err;
        }
        s_gpio_initialized = true;

        esp_err_t adc_err = adc_shared_init_channel(FLAME_AO_PIN);
        if (adc_err != ESP_OK) {
            ESP_LOGE(TAG, "Loi khoi tao ADC1_CH%d cho Flame AO: %d", FLAME_AO_PIN, adc_err);
            return adc_err;
        }
        ESP_LOGI(TAG, "Khoi tao Flame Sensor thanh cong (DO:%d Active-Low, AO:ADC1_CH%d)", FLAME_DO_PIN, FLAME_AO_PIN);
    } else {
        s_gpio_initialized = true;
        ESP_LOGW(TAG, "Flame Sensor DO chua gan chan (tam de GPIO_NUM_NC)");
    }
    return ESP_OK;
}

int flame_sensor_read_digital(void)
{
    if (FLAME_DO_PIN != GPIO_NUM_NC && FLAME_DO_PIN >= 0) {
        return gpio_get_level(FLAME_DO_PIN) == FLAME_DO_ACTIVE_LEVEL;
    }
    return 0; // Mặc định an toàn
}

esp_err_t flame_sensor_read_raw(int *raw_out)
{
    /* Legacy API: preserve historical DO-derived raw semantics. */
    if (raw_out) {
        int level = (FLAME_DO_PIN != GPIO_NUM_NC && FLAME_DO_PIN >= 0) ? gpio_get_level(FLAME_DO_PIN) : 1;
        *raw_out = (level == FLAME_DO_ACTIVE_LEVEL) ? 0 : 4095;
    }
    return ESP_OK;
}

esp_err_t flame_sensor_read_analog(int *raw_out, int *mv_out, bool *mv_valid_out)
{
    if (!s_gpio_initialized) return ESP_ERR_INVALID_STATE;
    return adc_shared_read(FLAME_AO_PIN, raw_out, mv_out, mv_valid_out);
}

esp_err_t flame_sensor_read(int *do_out, int *raw_out, flame_state_t *state_out, bool *valid, esp_err_t *err_code)
{
    if (!s_gpio_initialized) {
        if (valid) *valid = false;
        if (err_code) *err_code = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Đọc chân số DO (Active-Low)
    int do_level = (FLAME_DO_ACTIVE_LEVEL == 0) ? 1 : 0; // Chế độ chưa nối: mặc định an toàn
    if (FLAME_DO_PIN != GPIO_NUM_NC && FLAME_DO_PIN >= 0) {
        do_level = gpio_get_level(FLAME_DO_PIN);
    }
    if (do_out) *do_out = do_level;

    flame_state_t fstate = (do_level == FLAME_DO_ACTIVE_LEVEL) ? FLAME_STATE_FIRE : FLAME_STATE_SAFE;
    if (state_out) *state_out = fstate;

    // Legacy telemetry/risk input remains DO-derived until analog rules are explicitly approved.
    if (raw_out) {
        *raw_out = (fstate == FLAME_STATE_FIRE) ? 0 : 4095;
    }
    if (valid) *valid = true;
    if (err_code) *err_code = ESP_OK;

    return ESP_OK;
}
