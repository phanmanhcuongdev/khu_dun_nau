#include "buzzer.h"
#include "board_pins.h"
#include "safety_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";
static bool s_buzzer_state = false;
static bool s_initialized = false;
static buzzer_mode_t s_current_mode = BUZZER_MODE_OFF;
static int64_t s_last_pattern_toggle_us = 0;

esp_err_t buzzer_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Đặt mức logic LOW trước khi cấu hình pin để triệt tiêu tiếng click/bíp xung điện khi boot
    gpio_set_level(BUZZER_PIN, 0);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Kéo trở xuống giữ cực B ở 0V
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        s_buzzer_state = false;
        gpio_set_level(BUZZER_PIN, 0);
        s_current_mode = BUZZER_MODE_OFF;
        s_last_pattern_toggle_us = 0;
        s_initialized = true;
        ESP_LOGI(TAG, "Khoi tao mach coi BC337 thanh cong tren GPIO %d (Muc an toan: LOW)", BUZZER_PIN);
    } else {
        ESP_LOGE(TAG, "Loi gpio_config BUZZER (pin %d): %d", BUZZER_PIN, ret);
    }
    return ret;
}

void buzzer_set(bool on)
{
    s_buzzer_state = on;
    // Transistor BC337: Mức HIGH (1) kích dòng phân cực B-E làm bão hòa mối nối C-E -> Còi nối GND kêu
    // Mức LOW (0) ngắt transistor -> Còi tắt hoàn toàn
    gpio_set_level(BUZZER_PIN, on ? BUZZER_ACTIVE_LEVEL : 0);
}

void buzzer_toggle(void)
{
    buzzer_set(!s_buzzer_state);
}

bool buzzer_get_state(void)
{
    return s_buzzer_state;
}

void buzzer_beep(uint32_t duration_ms)
{
    buzzer_set(true);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_set(false);
}

void buzzer_set_mode(buzzer_mode_t mode)
{
    if (s_current_mode != mode) {
        s_current_mode = mode;
        s_last_pattern_toggle_us = 0; // Reset timer để áp dụng chế độ mới ngay lập tức
        if (mode == BUZZER_MODE_OFF) {
            buzzer_set(false);
        } else if (mode == BUZZER_MODE_CONTINUOUS) {
            buzzer_set(true);
        }
    }
}

buzzer_mode_t buzzer_get_mode(void)
{
    return s_current_mode;
}

void buzzer_update_pattern(int64_t now_us)
{
    if (!s_initialized) return;

    switch (s_current_mode) {
    case BUZZER_MODE_OFF:
        if (s_buzzer_state) {
            buzzer_set(false);
        }
        break;

    case BUZZER_MODE_CONTINUOUS:
        // Còi kêu liên tục 100% (EMERGENCY)
        if (!s_buzzer_state) {
            buzzer_set(true);
        }
        break;

    case BUZZER_MODE_INTERMITTENT: {
        // Nhịp ngắt quãng cảnh báo (WARNING): Bật 300ms, Tắt 400ms (Non-blocking)
        if (s_last_pattern_toggle_us == 0) {
            s_last_pattern_toggle_us = now_us;
            buzzer_set(true);
            break;
        }

        int64_t elapsed_us = now_us - s_last_pattern_toggle_us;
        if (s_buzzer_state) {
            if (elapsed_us >= (CONFIG_BUZZER_WARN_ON_MS * 1000LL)) {
                buzzer_set(false);
                s_last_pattern_toggle_us = now_us;
            }
        } else {
            if (elapsed_us >= (CONFIG_BUZZER_WARN_OFF_MS * 1000LL)) {
                buzzer_set(true);
                s_last_pattern_toggle_us = now_us;
            }
        }
        break;
    }

    case BUZZER_MODE_FAULT_BEEP: {
        // Bíp ngắn 100ms mỗi 5 giây nhắc nhở lỗi phần cứng (FAULT)
        if (s_last_pattern_toggle_us == 0) {
            s_last_pattern_toggle_us = now_us;
            buzzer_set(false);
            break;
        }

        int64_t elapsed_us = now_us - s_last_pattern_toggle_us;
        if (s_buzzer_state) {
            if (elapsed_us >= (CONFIG_BUZZER_FAULT_ON_MS * 1000LL)) {
                buzzer_set(false);
            }
        } else {
            if (elapsed_us >= (CONFIG_BUZZER_FAULT_PERIOD_MS * 1000LL)) {
                buzzer_set(true);
                s_last_pattern_toggle_us = now_us;
            }
        }
        break;
    }

    default:
        buzzer_set(false);
        break;
    }
}
