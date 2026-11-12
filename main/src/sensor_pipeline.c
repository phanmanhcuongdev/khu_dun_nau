#include "sensor_pipeline.h"
#include "board_pins.h"
#include "gy906_sensor.h"
#include "mq2_sensor.h"
#include "flame_sensor.h"
#include "ld2410_radar.h"
#include "touch_btn.h"
#include "buzzer.h"
#include "diagnostic_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PIPELINE";

// Monotonic Boot Time
static int64_t s_boot_time_us = 0;
static bool s_pipeline_initialized = false;

// Last Valid Timestamps (microsecond) for STALE detection
static int64_t s_gy906_last_valid_us = 0;
static int64_t s_mq2_last_valid_us   = 0;
static int64_t s_flame_last_valid_us = 0;
static int64_t s_radar_last_valid_us = 0;

// Filter Buffers (Window size = 3)
static float s_tobj_buf[FILTER_WINDOW_SIZE];
static uint8_t s_tobj_buf_count = 0;

static float s_ta_buf[FILTER_WINDOW_SIZE];
static uint8_t s_ta_buf_count = 0;

static int s_mq2_buf[FILTER_WINDOW_SIZE];
static uint8_t s_mq2_buf_count = 0;

// dT/dt Tracking Variables
static float s_prev_valid_tobj = 0.0f;
static int64_t s_prev_valid_tobj_time_us = 0;
static bool s_has_prev_valid_tobj = false;

// Hold Duration Timers (start timestamps in microsecond)
static int64_t s_high_temp_start_us     = 0;
static int64_t s_gas_start_us           = 0;
static int64_t s_no_presence_start_us   = 0;
static int64_t s_flame_start_us         = 0;
static int64_t s_sensor_error_start_us  = 0;

// Helper: 3-point Median Filter for float
static float calculate_median_3(float a, float b, float c)
{
    if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
    if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
    return c;
}

// Helper: Moving Average for int
static int calculate_avg_mq2(const int *buf, uint8_t count)
{
    if (count == 0) return 0;
    int32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        sum += buf[i];
    }
    return (int)(sum / count);
}

esp_err_t sensor_pipeline_init(void)
{
    s_boot_time_us = esp_timer_get_time();
    s_tobj_buf_count = 0;
    s_ta_buf_count = 0;
    s_mq2_buf_count = 0;
    s_has_prev_valid_tobj = false;
    s_high_temp_start_us = 0;
    s_gas_start_us = 0;
    s_no_presence_start_us = 0;
    s_flame_start_us = 0;
    s_sensor_error_start_us = 0;
    s_pipeline_initialized = true;

    ESP_LOGI(TAG, "Khoi tao Sensor Pipeline thanh cong (Boot timestamp: %lld us)", s_boot_time_us);
    return ESP_OK;
}

uint32_t sensor_pipeline_get_uptime_sec(void)
{
    if (s_boot_time_us == 0) return 0;
    return (uint32_t)((esp_timer_get_time() - s_boot_time_us) / 1000000);
}

const char* data_quality_to_str(data_quality_t q)
{
    switch (q) {
    case QUALITY_VALID:        return "VALID";
    case QUALITY_INVALID:      return "INVALID";
    case QUALITY_STALE:        return "STALE";
    case QUALITY_INITIALIZING: return "INIT";
    default:                   return "UNKNOWN";
    }
}

const char* v1_quality_to_str(v1_overall_quality_t q)
{
    switch (q) {
    case V1_QUALITY_OK:       return "OK";
    case V1_QUALITY_DEGRADED: return "DEGRADED";
    case V1_QUALITY_CRITICAL: return "CRITICAL";
    default:                  return "UNKNOWN";
    }
}

void sensor_pipeline_step(kitchen_sensor_data_t *data)
{
    if (!s_pipeline_initialized || data == NULL) return;

    int64_t now = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)((now - s_boot_time_us) / 1000000);
    data->system_uptime_sec = uptime_sec;

    // =========================================================================
    // 1. THU THẬP & ĐÁNH GIÁ CHẤT LƯỢNG GY-906 (MLX90614)
    // =========================================================================
    float ta_raw = 0.0f;
    float tobj_raw = 0.0f;
    bool gy_valid_read = false;
    esp_err_t gy_err = gy906_sensor_read(&ta_raw, &tobj_raw, &gy_valid_read, &data->gy906_err);

    data->gy906_last_update = now;
    data->gy906_new_data = true;

    if (gy_err == ESP_OK && gy_valid_read) {
        data->temp_ambient = ta_raw;
        data->temp_object = tobj_raw;
        data->gy906_quality = QUALITY_VALID;
        s_gy906_last_valid_us = now;

        // Đưa vào bộ lọc trung vị 3 mẫu (3-point Median Filter)
        if (s_tobj_buf_count < FILTER_WINDOW_SIZE) {
            s_tobj_buf[s_tobj_buf_count++] = tobj_raw;
            s_ta_buf[s_ta_buf_count++]     = ta_raw;
            data->temp_object_filtered  = tobj_raw;
            data->temp_ambient_filtered = ta_raw;
        } else {
            // Dịch chuyển buffer
            s_tobj_buf[0] = s_tobj_buf[1];
            s_tobj_buf[1] = s_tobj_buf[2];
            s_tobj_buf[2] = tobj_raw;

            s_ta_buf[0] = s_ta_buf[1];
            s_ta_buf[1] = s_ta_buf[2];
            s_ta_buf[2] = ta_raw;

            data->temp_object_filtered  = calculate_median_3(s_tobj_buf[0], s_tobj_buf[1], s_tobj_buf[2]);
            data->temp_ambient_filtered = calculate_median_3(s_ta_buf[0], s_ta_buf[1], s_ta_buf[2]);
        }
    } else {
        // Phân định rạch ròi INVALID và STALE
        if (s_gy906_last_valid_us > 0 && (now - s_gy906_last_valid_us) > (GY906_STALE_TIMEOUT_MS * 1000LL)) {
            data->gy906_quality = QUALITY_STALE;
        } else {
            data->gy906_quality = QUALITY_INVALID;
        }
    }
    data->gy906_valid = (data->gy906_quality == QUALITY_VALID);

    // =========================================================================
    // 2. THU THẬP & ĐÁNH GIÁ CHẤT LƯỢNG MQ-2 (WARM-UP & FILTER)
    // =========================================================================
    int mq2_raw_val = 0;
    int mq2_do_val = 1;
    bool mq2_gas_det = false;
    bool mq2_valid_read = false;
    esp_err_t mq2_err = mq2_sensor_read(&mq2_raw_val, &mq2_do_val, &mq2_gas_det, &mq2_valid_read, &data->mq2_err);

    data->mq2_last_update = now;
    data->mq2_new_data = true;
    data->mq2_raw = mq2_raw_val;
    data->mq2_do = mq2_do_val;
    data->mq2_gas_detected = mq2_gas_det;
    int mq2_ao_raw = -1, mq2_ao_mv = -1;
    bool mq2_ao_mv_valid = false;
    esp_err_t mq2_ao_err = mq2_sensor_read_analog(&mq2_ao_raw, &mq2_ao_mv, &mq2_ao_mv_valid);
    data->mq2_ao_adc_raw = mq2_ao_raw;
    data->mq2_ao_adc_mv = mq2_ao_mv;
    data->mq2_ao_valid = (mq2_ao_err == ESP_OK);
    data->mq2_ao_mv_valid = data->mq2_ao_valid && mq2_ao_mv_valid;

    // Kiểm tra trạng thái sấy cảm biến (Warm-up)
    if (uptime_sec < MQ2_WARMUP_TIME_SEC) {
        data->mq2_warming_up = true;
        data->mq2_warmup_remaining_sec = (uint32_t)(MQ2_WARMUP_TIME_SEC - uptime_sec);
        data->mq2_quality = QUALITY_INITIALIZING;
        data->mq2_valid = false;
        data->mq2_raw_filtered = mq2_raw_val;
    } else {
        data->mq2_warming_up = false;
        data->mq2_warmup_remaining_sec = 0;

        if (mq2_err == ESP_OK && mq2_valid_read) {
            data->mq2_quality = QUALITY_VALID;
            s_mq2_last_valid_us = now;

            // Bộ lọc trung bình di động 3 mẫu cho MQ-2
            if (s_mq2_buf_count < FILTER_WINDOW_SIZE) {
                s_mq2_buf[s_mq2_buf_count++] = mq2_raw_val;
            } else {
                s_mq2_buf[0] = s_mq2_buf[1];
                s_mq2_buf[1] = s_mq2_buf[2];
                s_mq2_buf[2] = mq2_raw_val;
            }
            data->mq2_raw_filtered = calculate_avg_mq2(s_mq2_buf, s_mq2_buf_count);
        } else {
            if (s_mq2_last_valid_us > 0 && (now - s_mq2_last_valid_us) > (MQ2_STALE_TIMEOUT_MS * 1000LL)) {
                data->mq2_quality = QUALITY_STALE;
            } else {
                data->mq2_quality = QUALITY_INVALID;
            }
        }
        data->mq2_valid = (data->mq2_quality == QUALITY_VALID);
    }

    // =========================================================================
    // 3. THU THẬP & ĐÁNH GIÁ CHẤT LƯỢNG FLAME SENSOR
    // =========================================================================
    int flame_do_val = 1;
    int flame_raw_val = -1;
    flame_state_t fstate = FLAME_STATE_SAFE;
    bool flame_valid_read = false;
    esp_err_t flame_err = flame_sensor_read(&flame_do_val, &flame_raw_val, &fstate, &flame_valid_read, &data->flame_err);

    data->flame_last_update = now;
    data->flame_new_data = true;
    data->flame_do = flame_do_val;
    data->flame_raw = flame_raw_val;
    data->flame_state = fstate;
    int flame_ao_raw = -1, flame_ao_mv = -1;
    bool flame_ao_mv_valid = false;
    esp_err_t flame_ao_err = flame_sensor_read_analog(&flame_ao_raw, &flame_ao_mv, &flame_ao_mv_valid);
    data->flame_ao_adc_raw = flame_ao_raw;
    data->flame_ao_adc_mv = flame_ao_mv;
    data->flame_ao_valid = (flame_ao_err == ESP_OK);
    data->flame_ao_mv_valid = data->flame_ao_valid && flame_ao_mv_valid;

    if (flame_err == ESP_OK && flame_valid_read) {
        data->flame_quality = QUALITY_VALID;
        s_flame_last_valid_us = now;
    } else {
        if (s_flame_last_valid_us > 0 && (now - s_flame_last_valid_us) > (FLAME_STALE_TIMEOUT_MS * 1000LL)) {
            data->flame_quality = QUALITY_STALE;
        } else {
            data->flame_quality = QUALITY_INVALID;
        }
    }
    data->flame_valid = (data->flame_quality == QUALITY_VALID);

    // =========================================================================
    // 4. THU THẬP & ĐÁNH GIÁ CHẤT LƯỢNG LD2410 RADAR
    // =========================================================================
    int presence_val = 0;
    int out_pin_val = 0;
    radar_target_mode_t rmode = RADAR_MODE_NO_TARGET;
    uint16_t m_dist = 0, s_dist = 0;
    uint8_t m_ener = 0, s_ener = 0;
    bool radar_valid_read = false;
    bool radar_uart_valid = false;

    esp_err_t radar_err = ld2410_radar_read(&presence_val, &out_pin_val, &rmode,
                                            &m_dist, &m_ener, &s_dist, &s_ener,
                                             &radar_valid_read, &radar_uart_valid, &data->radar_err);

    data->radar_last_update = now;
    data->radar_new_data = true;
    data->radar_presence = presence_val;
    data->radar_out_pin = out_pin_val;
    data->radar_mode = rmode;
    data->radar_move_dist = m_dist;
    data->radar_move_energy = m_ener;
    data->radar_stat_dist = s_dist;
    data->radar_stat_energy = s_ener;
    data->radar_uart_valid = radar_uart_valid;
    data->radar_uart_initialized = ld2410_radar_uart_is_initialized();
    data->radar_uart_rx_bytes = ld2410_radar_get_last_rx_bytes();
    data->radar_uart_frame_age_ms = ld2410_radar_get_last_frame_age_ms();

    if (radar_err == ESP_OK && radar_valid_read) {
        data->radar_quality = QUALITY_VALID;
        s_radar_last_valid_us = now;
    } else {
        if (s_radar_last_valid_us > 0 && (now - s_radar_last_valid_us) > (RADAR_STALE_TIMEOUT_MS * 1000LL)) {
            data->radar_quality = QUALITY_STALE;
        } else {
            data->radar_quality = QUALITY_INVALID;
        }
    }
    data->radar_valid = (data->radar_quality == QUALITY_VALID);

    // =========================================================================
    // 6. THU THẬP NÚT BẤM CẢM ỨNG TTP223 & CÒI BUZZER
    // =========================================================================
    bool touch_valid = false;
    touch_btn_read_state(&data->touch_state, &data->touch_edge, &touch_valid,
                         &data->touch_gpio_level);
    data->touch_last_update = now;
    data->touch_new_data = touch_valid;
    data->touch_quality = touch_valid ? QUALITY_VALID : QUALITY_INVALID;

    data->buzzer_state = buzzer_get_state() ? 1 : 0;
    data->buzzer_last_update = now;

    // =========================================================================
    // 7. TRÍCH XUẤT ĐẶC TRƯNG THỜI GIAN: dT/dt (°C/giây)
    // =========================================================================
    // Chỉ tính khi cả mẫu hiện tại và mẫu trước đều VALID, delta_t trong khoảng 0.5s .. 3.0s
    if (data->gy906_quality == QUALITY_VALID) {
        if (s_has_prev_valid_tobj) {
            float dt_sec = (float)(now - s_prev_valid_tobj_time_us) / 1000000.0f;
            if (dt_sec >= DT_DT_MIN_DELTA_TIME_SEC && dt_sec <= 3.0f) {
                data->dT_dt = (data->temp_object_filtered - s_prev_valid_tobj) / dt_sec;
                data->dT_dt_valid = true;

                s_prev_valid_tobj = data->temp_object_filtered;
                s_prev_valid_tobj_time_us = now;
            } else if (dt_sec > 3.0f) {
                // Quá 3s (mất chu kỳ hoặc trễ lớn) -> Rebase mốc mẫu, không tính delta qua khoảng trống
                s_prev_valid_tobj = data->temp_object_filtered;
                s_prev_valid_tobj_time_us = now;
                data->dT_dt = 0.0f;
                data->dT_dt_valid = false;
            }
        } else {
            // Mẫu hợp lệ đầu tiên sau boot hoặc sau khi phục hồi từ lỗi
            s_prev_valid_tobj = data->temp_object_filtered;
            s_prev_valid_tobj_time_us = now;
            s_has_prev_valid_tobj = true;
            data->dT_dt = 0.0f;
            data->dT_dt_valid = false;
        }
    } else {
        // Dữ liệu nhiệt độ không hợp lệ hoặc STALE: Hủy cờ dT/dt và reset mốc mẫu
        data->dT_dt_valid = false;
        data->dT_dt = 0.0f;
        s_has_prev_valid_tobj = false;
    }

    // =========================================================================
    // 8. THEO DÕI THỜI GIAN DUY TRÌ (HOLD DURATIONS: t_hold)
    // =========================================================================
    // 8.1 Thời gian duy trì nhiệt độ cao (> TEMP_HIGH_HOLD_THRESHOLD_C)
    if (data->gy906_quality == QUALITY_VALID && data->temp_object_filtered >= TEMP_HIGH_HOLD_THRESHOLD_C) {
        if (s_high_temp_start_us == 0) s_high_temp_start_us = now;
        data->t_hold_high_temp_ms = (uint32_t)((now - s_high_temp_start_us) / 1000);
    } else {
        s_high_temp_start_us = 0;
        data->t_hold_high_temp_ms = 0;
    }

    // 8.2 Thời gian duy trì phát hiện khí gas/khói
    if (data->mq2_quality == QUALITY_VALID && data->mq2_gas_detected) {
        if (s_gas_start_us == 0) s_gas_start_us = now;
        data->t_hold_gas_ms = (uint32_t)((now - s_gas_start_us) / 1000);
    } else {
        s_gas_start_us = 0;
        data->t_hold_gas_ms = 0;
    }

    // 8.3 Thời gian duy trì vắng người (Radar phải VALID và = 0)
    // Tuyệt đối không được coi việc mất kết nối Radar là không có người!
    if (data->radar_quality == QUALITY_VALID && data->radar_presence == 0) {
        if (s_no_presence_start_us == 0) s_no_presence_start_us = now;
        data->t_hold_no_presence_ms = (uint32_t)((now - s_no_presence_start_us) / 1000);
    } else {
        s_no_presence_start_us = 0;
        data->t_hold_no_presence_ms = 0;
    }

    // 8.4 Thời gian duy trì phát hiện ngọn lửa (FIRE)
    if (data->flame_quality == QUALITY_VALID && data->flame_state == FLAME_STATE_FIRE) {
        if (s_flame_start_us == 0) s_flame_start_us = now;
        data->t_hold_flame_ms = (uint32_t)((now - s_flame_start_us) / 1000);
    } else {
        s_flame_start_us = 0;
        data->t_hold_flame_ms = 0;
    }

    // 8.5 Thời gian duy trì có lỗi cảm biến (ít nhất 1 cảm biến INVALID hoặc STALE)
    bool has_sensor_error = (data->gy906_quality == QUALITY_INVALID || data->gy906_quality == QUALITY_STALE ||
                             data->mq2_quality   == QUALITY_INVALID || data->mq2_quality   == QUALITY_STALE ||
                             data->flame_quality == QUALITY_INVALID || data->flame_quality == QUALITY_STALE ||
                             data->radar_quality == QUALITY_INVALID || data->radar_quality == QUALITY_STALE);

    if (has_sensor_error) {
        if (s_sensor_error_start_us == 0) s_sensor_error_start_us = now;
        data->t_hold_sensor_error_ms = (uint32_t)((now - s_sensor_error_start_us) / 1000);
    } else {
        s_sensor_error_start_us = 0;
        data->t_hold_sensor_error_ms = 0;
    }

    // =========================================================================
    // 9. ĐÁNH GIÁ CHẤT LƯỢNG TỔNG HỢP VÙNG V1 (V1 AGGREGATED QUALITY)
    // =========================================================================
    int invalid_or_stale_count = 0;
    if (!data->gy906_valid) invalid_or_stale_count++;
    if (!data->mq2_valid)   invalid_or_stale_count++;
    if (!data->flame_valid) invalid_or_stale_count++;
    if (!data->radar_valid) invalid_or_stale_count++;

    // Mất cả 2 kênh cảnh báo cháy (Nhiệt độ + Lửa) hoặc hỏng >= 2 cảm biến -> CRITICAL
    if ((!data->gy906_valid && !data->flame_valid) || invalid_or_stale_count >= 2) {
        data->v1_quality = V1_QUALITY_CRITICAL;
    } else if (invalid_or_stale_count > 0 || data->mq2_warming_up) {
        data->v1_quality = V1_QUALITY_DEGRADED;
    } else {
        data->v1_quality = V1_QUALITY_OK;
    }

#if SENSOR_DIAG_LOG_ENABLED
    ESP_LOGI("DIAG_MQ2", "DO=%d gas=%d warmup=%d | AO raw=%d mv=%d valid=%d mv_valid=%d",
             data->mq2_do, data->mq2_gas_detected, data->mq2_warming_up,
             data->mq2_ao_adc_raw, data->mq2_ao_adc_mv,
             data->mq2_ao_valid, data->mq2_ao_mv_valid);
    ESP_LOGI("DIAG_FLAME", "DO=%d flame=%d | AO raw=%d mv=%d valid=%d mv_valid=%d",
             data->flame_do, data->flame_state, data->flame_ao_adc_raw,
             data->flame_ao_adc_mv, data->flame_ao_valid, data->flame_ao_mv_valid);
    if (data->radar_uart_valid) {
        ESP_LOGI("DIAG_RADAR", "OUT=%d presence=%d | UART init=%d valid=1 rx_bytes=%u target=%d moving=%ucm stationary=%ucm energy=%u/%u age_ms=%u",
                 data->radar_out_pin, data->radar_presence, data->radar_uart_initialized,
                 (unsigned)data->radar_uart_rx_bytes, data->radar_mode, data->radar_move_dist,
                 data->radar_stat_dist, data->radar_move_energy, data->radar_stat_energy,
                 (unsigned)data->radar_uart_frame_age_ms);
    } else {
        ESP_LOGI("DIAG_RADAR", "OUT=%d presence=%d | UART init=%d valid=0 rx_bytes=%u age_ms=%u",
                 data->radar_out_pin, data->radar_presence, data->radar_uart_initialized,
                 (unsigned)data->radar_uart_rx_bytes, (unsigned)data->radar_uart_frame_age_ms);
    }
    if (data->gy906_valid) {
        ESP_LOGI("DIAG_GY906", "object=%.2fC ambient=%.2fC valid=1 quality=%s",
                 (double)data->temp_object_filtered, (double)data->temp_ambient_filtered,
                 data_quality_to_str(data->gy906_quality));
    } else {
        ESP_LOGI("DIAG_GY906", "object=NA ambient=NA valid=0 quality=%s",
                 data_quality_to_str(data->gy906_quality));
    }
#endif
}
