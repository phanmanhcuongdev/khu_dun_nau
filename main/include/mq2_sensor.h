#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Khởi tạo cảm biến MQ-2 (GPIO 40 cho DO, ADC1_CH2 / GPIO 41 cho AO)
 */
esp_err_t mq2_sensor_init(void);

/**
 * @brief Đọc toàn diện thông số cảm biến MQ-2
 * @param raw_out Con trỏ nhận giá trị ADC thô (0 - 4095)
 * @param do_out Con trỏ nhận mức logic thô trên chân DO (0 hoặc 1)
 * @param gas_detected Con trỏ nhận cờ phát hiện khí gas (chuẩn hóa qua cực tính MQ2_DO_ACTIVE_LEVEL)
 * @param valid Con trỏ nhận trạng thái đọc hợp lệ
 * @param err_code Con trỏ nhận mã lỗi nếu có
 * @return esp_err_t ESP_OK nếu đọc ADC và GPIO thành công
 */
esp_err_t mq2_sensor_read(int *raw_out, int *do_out, bool *gas_detected, bool *valid, esp_err_t *err_code);

/**
 * @brief Đọc mức logic chân số DO
 */
int mq2_sensor_read_digital(void);

/**
 * @brief Đọc giá trị ADC thô kênh AO
 */
esp_err_t mq2_sensor_read_raw(int *raw_out);

/** Read the physical AO pin; millivolts is -1 when ADC calibration is unavailable. */
esp_err_t mq2_sensor_read_analog(int *raw_out, int *mv_out, bool *mv_valid_out);
