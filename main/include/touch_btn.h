#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Khởi tạo chân đọc nút cảm ứng TTP223 (GPIO 2 với pull-down chống trôi)
 */
esp_err_t touch_btn_init(void);

/**
 * @brief Đọc trạng thái nút cảm ứng và phát hiện cạnh (Edge Detection) kèm lọc nhiễu
 * @param state_out Debounced continuous level; ISR tap is reported only in edge_out.
 * @param edge_out Con trỏ nhận biến đổi cạnh:
 *                 1: Cạnh lên (vừa chạm ngón tay)
 *                -1: Cạnh xuống (vừa nhấc ngón tay)
 *                 0: Không đổi trạng thái
 * @param valid_out Con trỏ nhận trạng thái hợp lệ
 * @return esp_err_t ESP_OK nếu đọc thành công
 */
esp_err_t touch_btn_read_state(int *state_out, int *edge_out, bool *valid_out,
                               int *gpio_level_out);

/**
 * @brief Đọc trực tiếp mức logic chân cảm ứng TTP223
 */
int touch_btn_read(void);

/* Debounced continuous high duration from ANYEDGE tracking; never edge accumulation. */
uint32_t touch_btn_hold_ms(void);
