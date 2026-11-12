#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sensor_types.h"
#include "safety_config.h"
#include "risk_engine.h"
#include "buzzer.h"

/**
 * @brief Trạng thái điều phối cảnh báo cục bộ
 */
typedef struct {
    bool is_muted;                      // true nếu đang tạm tắt còi (chỉ có hiệu lực trong WARNING)
    uint32_t mute_remaining_sec;        // Số giây MUTE còn lại (giây)
    uint32_t touch_hold_duration_ms;    // Thời gian giữ nút TTP223 hiện tại (ms)
    bool ack_triggered;                 // Cờ báo vừa kích hoạt xác nhận giải tỏa EMERGENCY
    buzzer_mode_t active_buzzer_mode;   // Chế độ âm thanh còi đang chạy
} local_alarm_status_t;

/**
 * @brief Khởi tạo Local Alarm Controller
 */
esp_err_t local_alarm_controller_init(void);

/**
 * @brief Vòng lặp điều phối cảnh báo và tương tác tại chỗ:
 *  - Xử lý Mute WARNING khi người dùng chạm nút TTP223 (không mute vô hạn, tự phục hồi sau t_mute)
 *  - Hủy MUTE ngay lập tức khi chuyển sang EMERGENCY
 *  - Xử lý Long-Press Acknowledge giải tỏa EMERGENCY khi giữ TTP223 >= 3s
 *  - Điều khiển còi non-blocking theo cấp độ nguy cơ (EMERGENCY continuous, WARNING pulse, FAULT beep, NORMAL off)
 *  - Điều khiển hiển thị màn hình OLED chuyên sâu
 * 
 * @param snapshot Con trỏ dữ liệu cảm biến hợp nhất
 * @param wifi_connected Trạng thái kết nối Wi-Fi
 * @param mqtt_connected Trạng thái kết nối MQTT
 * @param out_status Con trỏ nhận trạng thái điều phối cảnh báo cục bộ
 */
void local_alarm_controller_step(kitchen_sensor_data_t *snapshot,
                                bool wifi_connected,
                                bool mqtt_connected,
                                local_alarm_status_t *out_status);
