#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sensor_types.h"

/**
 * @brief Khởi tạo cảm biến lửa (GPIO 21 cho DO, ADC2_CH9 / GPIO 20 cho AO)
 */
esp_err_t flame_sensor_init(void);

/**
 * @brief Đọc cảm biến ngọn lửa có kiểm tra hợp lệ và phân định trạng thái an toàn
 * @param do_out Con trỏ nhận mức logic thô trên chân DO (0: LOW, 1: HIGH)
 * @param raw_out Con trỏ nhận giá trị ADC thô (0 - 4095)
 * @param state_out Con trỏ nhận trạng thái FLAME_STATE_FIRE (1) hoặc FLAME_STATE_SAFE (0)
 * @param valid Con trỏ nhận cờ hợp lệ
 * @param err_code Con trỏ nhận mã lỗi
 * @return esp_err_t ESP_OK nếu đọc thành công
 */
esp_err_t flame_sensor_read(int *do_out, int *raw_out, flame_state_t *state_out, bool *valid, esp_err_t *err_code);

/**
 * @brief Đọc trạng thái phát hiện lửa (1 nếu có lửa, 0 nếu an toàn)
 */
int flame_sensor_read_digital(void);

/**
 * @brief Đọc giá trị ADC thô kênh quang AO
 */
esp_err_t flame_sensor_read_raw(int *raw_out);

/** Read the physical AO pin; millivolts is -1 when ADC calibration is unavailable. */
esp_err_t flame_sensor_read_analog(int *raw_out, int *mv_out, bool *mv_valid_out);
