#include "local_alarm_controller.h"
#include "oled_display.h"
#include "buzzer.h"
#include "touch_btn.h"
#include "diagnostic_config.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "LOCAL_ALARM";

// Trạng thái Mute Controller
static bool s_is_muted = false;
static int64_t s_mute_start_us = 0;

// Trạng thái nhấn giữ nút TTP223
static bool s_initialized = false;

esp_err_t local_alarm_controller_init(void)
{
    s_is_muted = false;
    s_mute_start_us = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "Khoi tao Local Alarm Controller thanh cong (Non-blocking coordinator)");
    return ESP_OK;
}

void local_alarm_controller_step(kitchen_sensor_data_t *snapshot,
                                bool wifi_connected,
                                bool mqtt_connected,
                                local_alarm_status_t *out_status)
{
    if (!s_initialized || snapshot == NULL) return;

    int64_t now_us = esp_timer_get_time();
    risk_level_t risk = snapshot->risk_state.risk_level;
    uint32_t mute_remaining_sec = 0;
    bool ack_success = false;

    // =========================================================================
    // 1. THEO DÕI THỜI GIAN NHẤN GIỮ NÚT TTP223 (LONG-PRESS ACKNOWLEDGE)
    // =========================================================================
    uint32_t hold_ms = snapshot->touch_quality==QUALITY_VALID ? touch_btn_hold_ms() : 0;
    ack_success = risk_engine_process_touch_hold(hold_ms);

    // =========================================================================
    // 2. MUTE CONTROLLER (ÁP DỤNG TRONG WARNING HOẶC FAULT, HỦY NGAY KHI EMERGENCY)
    // =========================================================================
    bool is_warning_or_fault = (risk == RISK_WARNING || snapshot->risk_state.monitoring_status == MONITORING_FAULT);

    if (risk == RISK_EMERGENCY) {
        // QUY TẮC BẮT BUỘC: Khi chuyển sang EMERGENCY -> Hủy MUTE ngay lập tức!
        if (s_is_muted) {
            ESP_LOGW(TAG, "EMERGENCY KICH HOAT -> HUY MUTE NGAY LAP TUC!");
            s_is_muted = false;
            s_mute_start_us = 0;
        }
    } else if (is_warning_or_fault) {
        // Chạm nút (cạnh lên hoặc đang chạm nhả < 2s) sẽ kích hoạt MUTE tạm thời 60 giây
        bool touch_detected = (snapshot->touch_edge == 1 || snapshot->touch_state == 1);
        if (touch_detected && !s_is_muted && hold_ms < 2000) {
            s_is_muted = true;
            s_mute_start_us = now_us;
            ESP_LOGW(TAG, "TTP223: Da tam MUTE coi thanh cong trong %d giay!", CONFIG_MUTE_DURATION_SEC);
        }

        if (s_is_muted) {
            int64_t elapsed_mute_us = now_us - s_mute_start_us;
            int64_t max_mute_us = (CONFIG_MUTE_DURATION_SEC * 1000000LL);

            if (elapsed_mute_us >= max_mute_us) {
                // Hết thời gian Mute -> Tự động khôi phục chuông còi nếu cảnh báo vẫn còn
                s_is_muted = false;
                s_mute_start_us = 0;
                mute_remaining_sec = 0;
                ESP_LOGW(TAG, "Het thoi gian MUTE (%d giay) -> Khoi phuc coi!", CONFIG_MUTE_DURATION_SEC);
            } else {
                mute_remaining_sec = (uint32_t)((max_mute_us - elapsed_mute_us) / 1000000LL) + 1;
            }
        }
    } else {
        // Khi không ở WARNING hay FAULT -> Tắt Mute
        s_is_muted = false;
        s_mute_start_us = 0;
        mute_remaining_sec = 0;
    }

    // =========================================================================
    // 3. ĐIỀU PHỐI ÂM THANH CÒI BUZZER TẬP TRUNG (NON-BLOCKING)
    // =========================================================================
    buzzer_mode_t target_mode = BUZZER_MODE_OFF;

    if (risk == RISK_EMERGENCY) {
        // EMERGENCY: Âm thanh liên tục, ưu tiên cao nhất, không bị Mute
        target_mode = BUZZER_MODE_CONTINUOUS;
    } else if (risk == RISK_WARNING) {
        // WARNING: Âm thanh ngắt quãng nếu chưa Mute
        if (s_is_muted) {
            target_mode = BUZZER_MODE_OFF;
        } else {
            target_mode = BUZZER_MODE_INTERMITTENT;
        }
    } else if (risk == RISK_SUSPICIOUS) {
        // SUSPICIOUS: Không bật còi báo động để tránh ồn ào không cần thiết
        target_mode = BUZZER_MODE_OFF;
    } else {
        // NORMAL / UNKNOWN:
        // Nếu phần cứng cảm biến bị FAULT, bíp ngắt quãng thưa nhắc nhở (cũng tôn trọng MUTE)
        if (snapshot->risk_state.monitoring_status == MONITORING_FAULT) {
            target_mode = s_is_muted ? BUZZER_MODE_OFF : BUZZER_MODE_FAULT_BEEP;
        } else {
            target_mode = BUZZER_MODE_OFF;
        }
    }

    // Áp dụng chế độ và cập nhật nhịp phát xung còi non-blocking
    buzzer_set_mode(target_mode);
    buzzer_update_pattern(now_us);
    snapshot->buzzer_state = buzzer_get_state() ? 1 : 0;
    snapshot->is_muted = s_is_muted;
    snapshot->mute_remaining_sec = mute_remaining_sec;

#if SENSOR_DIAG_LOG_ENABLED
    ESP_LOGI("DIAG_TOUCH", "gpio=%d touch=%d edge=%d mute=%d ack=%d",
             snapshot->touch_gpio_level, snapshot->touch_state, snapshot->touch_edge,
             s_is_muted, ack_success);
#endif

    // =========================================================================
    // 4. ĐIỀU PHỐI MÀN HÌNH HIỂN THỊ OLED TẬP TRUNG
    // =========================================================================
    oled_display_update(snapshot, wifi_connected, mqtt_connected);

    // =========================================================================
    // 5. XUẤT TRẠNG THÁI RA STRUCT ĐIỀU PHỐI CỤC BỘ
    // =========================================================================
    if (out_status != NULL) {
        out_status->is_muted = s_is_muted;
        out_status->mute_remaining_sec = mute_remaining_sec;
        out_status->touch_hold_duration_ms = hold_ms;
        out_status->ack_triggered = ack_success;
        out_status->active_buzzer_mode = target_mode;
    }
}
