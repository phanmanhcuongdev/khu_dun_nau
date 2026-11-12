#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Trạng thái chất lượng dữ liệu của từng cảm biến (Data Quality)
 */
typedef enum {
    QUALITY_INITIALIZING = 0, // Đang khởi động / warm-up, chưa đủ mẫu ổn định
    QUALITY_VALID        = 1, // Dữ liệu đọc thành công, nằm trong miền kỹ thuật hợp lệ
    QUALITY_INVALID      = 2, // Đọc thất bại, lỗi checksum/frame, ngoài dải đo kỹ thuật
    QUALITY_STALE        = 3, // Dữ liệu quá hạn (quá timeout cấu hình không nhận được mẫu mới)
} data_quality_t;

/**
 * @brief Trạng thái chất lượng tổng hợp của vùng giám sát V1
 */
typedef enum {
    V1_QUALITY_OK       = 0, // Tất cả các cảm biến trọng yếu đều VALID
    V1_QUALITY_DEGRADED = 1, // Có 1 hoặc vài cảm biến bị INVALID/STALE nhưng hệ thống vẫn còn cảm biến bù đắp
    V1_QUALITY_CRITICAL = 2, // Mất các cảm biến an toàn cốt lõi hoặc đa số cảm biến bị lỗi
} v1_overall_quality_t;

/**
 * @brief Định nghĩa 5 cấp độ nguy cơ an toàn khu vực đun nấu (Risk Level)
 */
typedef enum {
    RISK_UNKNOWN    = 0,    // Trạng thái khởi tạo, chưa đủ dữ liệu hoặc đang warm-up
    RISK_NORMAL     = 1,    // An toàn: Cảm biến hợp lệ, bếp nguội hoặc có người trông coi bình thường
    RISK_SUSPICIOUS = 2,    // Nghi ngờ: Bếp đang nóng/đun nấu nhưng vắng người kéo dài
    RISK_WARNING    = 3,    // Cảnh báo: Nhiệt độ vượt ngưỡng cảnh báo hoặc tăng nhanh kèm theo gas/khói
    RISK_EMERGENCY  = 4,    // Khẩn cấp: Có ngọn lửa bùng phát hoặc nhiệt độ vượt ngưỡng tới hạn cháy
} risk_level_t;

/**
 * @brief Định nghĩa trạng thái sẵn sàng của hệ thống giám sát (Monitoring Status)
 * Độc lập hoàn toàn với Risk Level.
 */
typedef enum {
    MONITORING_OK       = 0,    // Tất cả cảm biến bắt buộc hoạt động đầy đủ và hợp lệ
    MONITORING_DEGRADED = 1,    // Mất một số cảm biến hỗ trợ nhưng vẫn đủ đầu vào để chạy luật nguy cơ
    MONITORING_FAULT    = 2,    // Mất GY-906 hoặc không còn đủ cảm biến để đánh giá nguy cơ
} monitoring_status_t;

/**
 * @brief Định nghĩa nguyên nhân kích hoạt hoặc chuyển trạng thái (Reason Engine)
 */
typedef enum {
    REASON_NONE                     = 0,
    REASON_INITIALIZING             = 1,    // Hệ thống đang khởi động hoặc cảm biến đang sấy nóng
    REASON_FIRE_DETECTED            = 2,    // Phát hiện ngọn lửa (Flame DO = LOW)
    REASON_CRITICAL_TEMPERATURE     = 3,    // Nhiệt độ vượt ngưỡng tới hạn khẩn cấp (>= T_emergency)
    REASON_HIGH_TEMPERATURE         = 4,    // Nhiệt độ cao vượt ngưỡng cảnh báo (>= T_warn)
    REASON_RAPID_TEMPERATURE_RISE   = 5,    // Nhiệt độ tăng vọt đột biến kèm theo khí gas/khói
    REASON_UNATTENDED_COOKING       = 6,    // Bỏ quên bếp: Bếp đang nóng nhưng vắng người quá thời gian quy định
    REASON_GAS_SMOKE_ELEVATED       = 7,    // Phát hiện nồng độ khí gas / khói tăng cao
    REASON_SENSOR_FAULT             = 8,    // Cảm biến cốt lõi bị lỗi hoặc đứt kết nối
    REASON_SENSOR_STALE             = 9,    // Dữ liệu cảm biến bị treo/quá hạn timeout
    REASON_RECOVERY_PENDING         = 10,   // Đang trong thời gian kiểm chứng an toàn liên tục để hạ mức
    REASON_EMERGENCY_LATCHED        = 11,   // Điều kiện nguy hiểm đã hạ nhưng còi/chốt khẩn cấp chờ người xác nhận tại chỗ
    REASON_EMERGENCY_ACKNOWLEDGED   = 12,   // Đã nhận lệnh xác nhận giải tỏa tại chỗ từ người dùng
    REASON_NORMAL_SAFE              = 13,   // Mọi thông số an toàn bình thường
} risk_reason_t;

/**
 * @brief Cấu trúc lưu trữ kết quả đánh giá của Edge Risk Engine
 */
typedef enum { DETECTION_UNKNOWN, DETECTION_NO_EMERGENCY, DETECTION_EMERGENCY_PRESENT } detection_state_t;
typedef struct {
    detection_state_t detection_state;
    bool ack_eligible;
    uint32_t physical_ack_seq;
    int persistence_health; /* latch_store_health_t; read-only, not cloud control */
    risk_level_t risk_level;
    monitoring_status_t monitoring_status;
    risk_reason_t risk_reason;
    bool emergency_latched;             // true nếu trạng thái EMERGENCY đang bị chốt (phải xác nhận tại chỗ mới gỡ)
    bool emergency_acknowledged;        // true nếu vừa nhận tín hiệu xác nhận tại chỗ
    uint32_t recovery_countdown_sec;    // Đếm ngược thời gian an toàn liên tục để hạ mức (giây)
    int64_t last_eval_time_us;          // Thời điểm đánh giá cuối (microsecond)
} risk_state_t;

/**
 * @brief Định nghĩa chế độ mục tiêu của Radar LD2410
 */
typedef enum {
    RADAR_MODE_NO_TARGET = 0x00,
    RADAR_MODE_MOVING    = 0x01,
    RADAR_MODE_STATIC    = 0x02,
    RADAR_MODE_BOTH      = 0x03,
} radar_target_mode_t;

/**
 * @brief Trạng thái phát hiện ngọn lửa
 */
typedef enum {
    FLAME_STATE_SAFE = 0,
    FLAME_STATE_FIRE = 1,
} flame_state_t;

/**
 * @brief Cấu trúc dữ liệu hợp nhất chuẩn hóa cho toàn bộ hệ thống cảm biến khu đun nấu
 */
typedef struct {
    // =========================================================================
    // 1. GY906 (MLX90614) - Cảm biến nhiệt độ hồng ngoại không tiếp xúc
    // =========================================================================
    float temp_ambient;             // Nhiệt độ môi trường thô (°C)
    float temp_object;              // Nhiệt độ vật thể thô (°C)
    float temp_ambient_filtered;    // Nhiệt độ môi trường đã qua bộ lọc (°C)
    float temp_object_filtered;     // Nhiệt độ vật thể đã qua bộ lọc (°C)
    data_quality_t gy906_quality;   // Trạng thái chất lượng: VALID, INVALID, STALE, INITIALIZING
    bool gy906_valid;               // Tiện ích tương thích: true khi gy906_quality == QUALITY_VALID
    esp_err_t gy906_err;            // Mã lỗi chi tiết
    int64_t gy906_last_update;      // Thời điểm cập nhật cuối (microsecond)
    bool gy906_new_data;            // Cờ dữ liệu mới

    // =========================================================================
    // 2. MQ-2 - Cảm biến khí gas và khói
    // =========================================================================
    int mq2_raw;                    // Giá trị ADC thô 12-bit (0 - 4095)
    int mq2_raw_filtered;           // Giá trị ADC đã qua bộ lọc trung bình
    int mq2_do;                     // Mức logic chân DO (0 hoặc 1)
    bool mq2_gas_detected;          // true nếu vượt ngưỡng nồng độ cảnh báo
    data_quality_t mq2_quality;     // Trạng thái chất lượng: VALID, INVALID, STALE, INITIALIZING (warm-up)
    bool mq2_valid;                 // Tiện ích: true khi mq2_quality == QUALITY_VALID
    bool mq2_warming_up;            // true nếu đang trong thời gian sấy cảm biến (warm-up)
    uint32_t mq2_warmup_remaining_sec; // Số giây sấy cảm biến còn lại
    esp_err_t mq2_err;              // Mã lỗi chi tiết
    int64_t mq2_last_update;        // Thời điểm cập nhật cuối (microsecond)
    bool mq2_new_data;              // Cờ dữ liệu mới
    int mq2_ao_adc_raw;             // AO physical ADC1 sample; -1 when unavailable
    int mq2_ao_adc_mv;              // Calibrated ADC-pin mV; -1 when calibration unavailable
    bool mq2_ao_valid;              // true only for a successful physical AO sample
    bool mq2_ao_mv_valid;           // true only when calibration produced mV

    // =========================================================================
    // 3. Flame Sensor - Cảm biến ngọn lửa hồng ngoại
    // =========================================================================
    int flame_do;                   // Mức logic chân DO (0: LOW, 1: HIGH)
    int flame_raw;                  // Giá trị ADC thô 12-bit từ chân AO
    flame_state_t flame_state;      // FLAME_STATE_FIRE (1) khi phát hiện lửa, FLAME_STATE_SAFE (0) khi an toàn
    data_quality_t flame_quality;   // Trạng thái chất lượng
    bool flame_valid;               // Tiện ích
    esp_err_t flame_err;            // Mã lỗi
    int64_t flame_last_update;      // Thời điểm cập nhật cuối (microsecond)
    bool flame_new_data;            // Cờ dữ liệu mới
    int flame_ao_adc_raw;           // AO physical ADC1 sample; -1 when unavailable
    int flame_ao_adc_mv;            // Calibrated ADC-pin mV; -1 when calibration unavailable
    bool flame_ao_valid;            // true only for a successful physical AO sample
    bool flame_ao_mv_valid;         // true only when calibration produced mV

    // =========================================================================
    // 4. HLK-LD2410 - Radar sóng milimet 24GHz phát hiện hiện diện
    // =========================================================================
    int radar_presence;             // 1: Có người hiện diện, 0: Không có người
    int radar_out_pin;              // Mức logic đọc trực tiếp từ chân phần cứng OUT (GPIO 7)
    radar_target_mode_t radar_mode; // Chế độ mục tiêu: NO_TARGET (0), MOVING (1), STATIC (2), BOTH (3)
    uint16_t radar_move_dist;       // Khoảng cách mục tiêu di chuyển (cm)
    uint8_t radar_move_energy;      // Năng lượng phản xạ mục tiêu di chuyển (0 - 100)
    uint16_t radar_stat_dist;       // Khoảng cách mục tiêu đứng yên (cm)
    uint8_t radar_stat_energy;      // Năng lượng phản xạ mục tiêu đứng yên (0 - 100)
    data_quality_t radar_quality;   // Trạng thái chất lượng: VALID, INVALID, STALE
    bool radar_valid;               // Tiện ích
    esp_err_t radar_err;            // Mã lỗi giao tiếp UART
    int64_t radar_last_update;      // Thời điểm cập nhật cuối (microsecond)
    bool radar_new_data;            // Cờ dữ liệu mới
    bool radar_uart_valid;          // true only when a complete validated UART frame was parsed
    bool radar_uart_initialized;    // UART driver and pin routing configured successfully
    size_t radar_uart_rx_bytes;     // Actual bytes returned by uart_read_bytes this cycle
    uint32_t radar_uart_frame_age_ms; // UINT32_MAX when no valid UART frame has been received

    // =========================================================================
    // 5. TTP223 - Nút cảm ứng điện dung
    // =========================================================================
    int touch_state;                // 1: Đang chạm (TOUCH), 0: Nhả (RELEASE)
    int touch_gpio_level;           // Raw GPIO sample from TTP223 at this pipeline cycle
    int touch_edge;                 // 1: Cạnh lên (vừa chạm), -1: Cạnh xuống (vừa nhả), 0: Giữ nguyên
    data_quality_t touch_quality;   // Trạng thái chất lượng
    int64_t touch_last_update;      // Thời điểm cập nhật cuối (microsecond)
    bool touch_new_data;            // Cờ dữ liệu mới

    // =========================================================================
    // 6. Buzzer - Cơ cấu chấp hành còi báo động (BC337)
    // =========================================================================
    int buzzer_state;               // 1: Đang bật, 0: Đang tắt
    int64_t buzzer_last_update;     // Thời điểm cập nhật trạng thái còi cuối cùng

    // =========================================================================
    // 7. Temporal Features & System Diagnostics (Đặc trưng thời gian & Chẩn đoán)
    // =========================================================================
    float dT_dt;                    // Tốc độ thay đổi nhiệt độ vật thể (°C/giây)
    bool dT_dt_valid;               // true nếu tính toán thành công từ 2 mẫu liên tiếp đều VALID
    uint32_t t_hold_high_temp_ms;   // Thời gian duy trì nhiệt độ cao liên tục (ms)
    uint32_t t_hold_gas_ms;         // Thời gian duy trì phát hiện gas liên tục (ms)
    uint32_t t_hold_no_presence_ms; // Thời gian duy trì không có người liên tục (ms)
    uint32_t t_hold_flame_ms;       // Thời gian duy trì phát hiện lửa liên tục (ms)
    uint32_t t_hold_sensor_error_ms;// Thời gian duy trì cảm biến lỗi liên tục (ms)

    uint32_t system_uptime_sec;     // Thời gian hoạt động từ khi boot (giây)
    v1_overall_quality_t v1_quality;// Đánh giá chất lượng tổng hợp vùng V1

    // =========================================================================
    // 8. Edge Risk Engine State (Đánh giá nguy cơ tại biên)
    // =========================================================================
    risk_state_t risk_state;        // Trạng thái nguy cơ, giám sát và lý do

    // =========================================================================
    // 9. Local Alarm & Mute State (Trạng thái điều phối cảnh báo cục bộ)
    // =========================================================================
    bool is_muted;                  // true nếu đang tạm tắt chuông (chỉ có hiệu lực trong WARNING)
    uint32_t mute_remaining_sec;    // Số giây MUTE còn lại (giây)
} kitchen_sensor_data_t;
