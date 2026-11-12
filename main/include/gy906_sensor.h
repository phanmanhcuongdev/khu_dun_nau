#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Giới hạn kỹ thuật theo datasheet Melexis MLX90614
#define GY906_TA_MIN_DEG_C      (-40.0f)
#define GY906_TA_MAX_DEG_C      (125.0f)
#define GY906_TOBJ_MIN_DEG_C    (-70.0f)
#define GY906_TOBJ_MAX_DEG_C    (382.2f)

/**
 * @brief Khởi tạo bus I2C0 cho cảm biến nhiệt độ hồng ngoại GY-906 (MLX90614)
 */
esp_err_t gy906_sensor_init(void);

/**
 * @brief Đọc nhiệt độ môi trường (TA) và nhiệt độ vật thể (TOBJ1) có kiểm tra hợp lệ
 * @param ambient Con trỏ nhận giá trị nhiệt độ môi trường (°C)
 * @param object Con trỏ nhận giá trị nhiệt độ vật thể (°C)
 * @param valid Con trỏ nhận trạng thái hợp lệ
 * @param err_code Con trỏ nhận mã lỗi chi tiết
 * @return ESP_OK nếu đọc thành công và hợp lệ, ngược lại trả về mã lỗi
 */
esp_err_t gy906_sensor_read(float *ambient, float *object, bool *valid, esp_err_t *err_code);

/**
 * @brief Hàm tương thích ngược đọc nhiệt độ
 */
esp_err_t gy906_read_temperatures(float *ambient, float *object);
