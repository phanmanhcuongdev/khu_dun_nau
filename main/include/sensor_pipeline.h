#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sensor_types.h"
#include "safety_config.h"

// =============================================================================
// CẤU HÌNH THỜI GIAN VÀ BỘ LỌC TẬP TRUNG
// =============================================================================
#define SAMPLING_INTERVAL_MS            1000    // Chu kỳ lấy mẫu mặc định 1000 ms
#define MQ2_WARMUP_TIME_SEC             CONFIG_MQ2_WARMUP_TIME_SEC // Rút ngắn còn 3 giây theo safety_config.h
#define FILTER_WINDOW_SIZE              3       // Cửa sổ lọc 3 mẫu (lọc nhiễu đột biến, phản ứng nhanh với cháy)
#define DT_DT_MIN_DELTA_TIME_SEC        0.5f    // Khoảng cách thời gian tối thiểu để tính dT/dt (tránh chia số quá nhỏ)
#define TEMP_HIGH_HOLD_THRESHOLD_C      70.0f   // Ngưỡng nhiệt độ vật thể để tính thời gian duy trì t_hold_high_temp (°C)

// Timeout coi dữ liệu là STALE nếu không nhận được mẫu mới (millisecond)
#define GY906_STALE_TIMEOUT_MS          3000
#define MQ2_STALE_TIMEOUT_MS            3000
#define FLAME_STALE_TIMEOUT_MS          2500
#define RADAR_STALE_TIMEOUT_MS          3000
#define TOUCH_STALE_TIMEOUT_MS          2000

/**
 * @brief Khởi tạo hệ thống thu thập, lọc và theo dõi đặc trưng thời gian
 */
esp_err_t sensor_pipeline_init(void);

/**
 * @brief Thực thi một chu kỳ của Pipeline xử lý cảm biến:
 * 1. Thu thập dữ liệu cảm biến độc lập theo chu kỳ (không blocking).
 * 2. Cập nhật thời gian uptime và tiến trình sấy warm-up MQ-2.
 * 3. Đánh giá chất lượng dữ liệu từng cảm biến (VALID, INVALID, STALE).
 * 4. Lọc số (Moving Average / Median Filter) cho nhiệt độ và gas, giữ nguyên dữ liệu thô.
 * 5. Trích xuất đặc trưng thời gian: tính dT/dt (°C/s) và các bộ đếm thời gian duy trì t_hold.
 * 6. Tổng hợp chất lượng giám sát toàn vùng V1.
 * 
 * @param data Con trỏ struct chứa dữ liệu cảm biến hợp nhất
 */
void sensor_pipeline_step(kitchen_sensor_data_t *data);

/**
 * @brief Lấy thời gian hoạt động của hệ thống (Uptime) theo giây
 */
uint32_t sensor_pipeline_get_uptime_sec(void);

/**
 * @brief Chuyển đổi enum Data Quality sang chuỗi văn bản phục vụ hiển thị / log
 */
const char* data_quality_to_str(data_quality_t q);

/**
 * @brief Chuyển đổi enum V1 Overall Quality sang chuỗi văn bản
 */
const char* v1_quality_to_str(v1_overall_quality_t q);
