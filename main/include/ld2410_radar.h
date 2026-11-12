#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sensor_types.h"

/**
 * @brief Khởi tạo Radar LD2410 trên UART1 (256000 bps, 8-N-1) và chân OUT (GPIO 8)
 */
esp_err_t ld2410_radar_init(void);

/**
 * @brief Đọc toàn diện thông tin Radar LD2410 (kết hợp phân tích frame UART và chân OUT)
 * @param presence_out Con trỏ nhận trạng thái hiện diện (1: có người, 0: không có người)
 * @param out_pin_val Con trỏ nhận mức logic chân phần cứng OUT
 * @param mode_out Con trỏ nhận chế độ mục tiêu (NO_TARGET, MOVING, STATIC, BOTH)
 * @param move_dist Con trỏ nhận khoảng cách mục tiêu di chuyển (cm)
 * @param move_energy Con trỏ nhận mức năng lượng di chuyển (0 - 100)
 * @param stat_dist Con trỏ nhận khoảng cách mục tiêu đứng yên (cm)
 * @param stat_energy Con trỏ nhận mức năng lượng đứng yên (0 - 100)
 * @param valid Con trỏ nhận cờ hợp lệ (true khi OUT fallback hoặc measurement hợp lệ; uart_valid chỉ cho measurement)
 * @param err_code Con trỏ nhận mã lỗi
 * @return esp_err_t ESP_OK nếu đọc và giải mã frame thành công
 */
esp_err_t ld2410_radar_read(int *presence_out, int *out_pin_val, radar_target_mode_t *mode_out,
                            uint16_t *move_dist, uint8_t *move_energy,
                            uint16_t *stat_dist, uint8_t *stat_energy,
                             bool *valid, bool *uart_valid, esp_err_t *err_code);

/**
 * @brief Đọc trực tiếp mức logic chân OUT (GPIO 8) của radar
 */
int ld2410_read_out_pin(void);

bool ld2410_radar_uart_is_initialized(void);
size_t ld2410_radar_get_last_rx_bytes(void);
uint32_t ld2410_radar_get_last_frame_age_ms(void);

/**
 * @brief Giải mã gói tin target frame từ buffer UART (tương thích ngược)
 */
bool ld2410_parse_target_data(uint8_t *target_state, uint16_t *move_dist, 
                              uint8_t *move_energy, uint16_t *stat_dist, 
                              uint8_t *stat_energy);
