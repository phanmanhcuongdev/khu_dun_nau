#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Định nghĩa các chế độ phát âm thanh của còi báo động (Buzzer Pattern Mode)
 */
typedef enum {
    BUZZER_MODE_OFF = 0,        // Tắt còi hoàn toàn (NORMAL hoặc Mute)
    BUZZER_MODE_CONTINUOUS,     // Kêu liên tục 100% (EMERGENCY - Ưu tiên cao nhất)
    BUZZER_MODE_INTERMITTENT,   // Kêu ngắt quãng nhịp Bíp-Ngắt-Bíp-Ngắt (WARNING)
    BUZZER_MODE_FAULT_BEEP,     // Bíp ngắn định kỳ nhắc lỗi phần cứng (FAULT)
} buzzer_mode_t;

/**
 * @brief Khởi tạo mạch điều khiển còi qua transistor NPN BC337 (GPIO 48)
 * Đảm bảo chân GPIO ở mức LOW an toàn ngay khi khởi động.
 */
esp_err_t buzzer_init(void);

/**
 * @brief Bật hoặc tắt còi trực tiếp
 * @param on true: bật còi (mức 1 kích dẫn BC337), false: tắt còi (mức 0 ngắt transistor)
 */
void buzzer_set(bool on);

/**
 * @brief Đảo trạng thái còi (đang bật -> tắt, đang tắt -> bật)
 */
void buzzer_toggle(void);

/**
 * @brief Lấy trạng thái chân điều khiển còi hiện tại
 * @return true nếu còi đang được cấp mức HIGH, false nếu LOW
 */
bool buzzer_get_state(void);

/**
 * @brief Phát tiếng bíp ngắn (blocking delay - chỉ dùng khi boot)
 * @param duration_ms Thời gian kêu tính bằng millisecond
 */
void buzzer_beep(uint32_t duration_ms);

/**
 * @brief Cài đặt chế độ âm thanh còi tập trung (Buzzer Pattern)
 * @param mode Chế độ còi (OFF, CONTINUOUS, INTERMITTENT, FAULT_BEEP)
 */
void buzzer_set_mode(buzzer_mode_t mode);

/**
 * @brief Lấy chế độ âm thanh còi hiện tại
 */
buzzer_mode_t buzzer_get_mode(void);

/**
 * @brief Cập nhật nhịp còi non-blocking dựa trên timestamp hiện tại
 * Tuyệt đối không dùng delay blocking làm treo hệ thống.
 * 
 * @param now_us Monotonic timestamp từ esp_timer_get_time() (microsecond)
 */
void buzzer_update_pattern(int64_t now_us);
