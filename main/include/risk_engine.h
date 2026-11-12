#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sensor_types.h"
#include "safety_config.h"

/**
 * @brief Khởi tạo Edge Risk Engine
 */
esp_err_t risk_engine_init(void);
/* Call only in safety task AFTER local alarm actuation; enqueue NVS I/O. */
void risk_engine_persistence_step(void);

/**
 * @brief Thực thi đánh giá nguy cơ và trạng thái giám sát tại biên (Edge Risk Evaluation)
 * Tuân thủ nghiêm ngặt nguyên tắc:
 *  - Không đọc GPIO trực tiếp.
 *  - Không điều khiển còi trực tiếp.
 *  - Không gọi MQTT hay OLED.
 *  - Đầu vào thuần túy là sensor snapshot đã chuẩn hóa.
 * 
 * @param snapshot Con trỏ chứa toàn bộ dữ liệu cảm biến đã qua pipeline
 * @param out_state Con trỏ nhận kết quả đánh giá rủi ro và giám sát
 */
void risk_engine_step(const kitchen_sensor_data_t *snapshot, risk_state_t *out_state);

/**
 * @brief Xử lý thời gian nhấn giữ nút cảm ứng TTP223 để tự động kích hoạt acknowledge khi đủ thời gian long-press
 * @param touch_hold_time_ms Thời gian giữ nút tính bằng millisecond
 * @return true nếu request physical hold được nhận; durable clear xảy ra bất đồng bộ
 */
bool risk_engine_process_touch_hold(uint32_t touch_hold_time_ms);

/**
 * @brief Chuyển đổi Risk Level sang chuỗi văn bản
 */
const char* risk_level_to_str(risk_level_t level);

/**
 * @brief Chuyển đổi Monitoring Status sang chuỗi văn bản
 */
const char* monitoring_status_to_str(monitoring_status_t status);

/**
 * @brief Chuyển đổi Risk Reason sang chuỗi văn bản phục vụ hiển thị OLED và Telemetry
 */
const char* risk_reason_to_str(risk_reason_t reason);
