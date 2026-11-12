#pragma once

// =============================================================================
// CẤU HÌNH NGƯỠNG AN TOÀN TẬP TRUNG (CENTRALIZED SAFETY THRESHOLDS)
// DỰ ÁN GIÁM SÁT AN TOÀN KHU VỰC ĐUN NẤU V1
// =============================================================================

// -----------------------------------------------------------------------------
// 1. NGƯỠNG NHIỆT ĐỘ BỀ MẶT BẾP / VẬT THỂ (°C)
// -----------------------------------------------------------------------------
#define CONFIG_TEMP_ACTIVE_C                55.0f   // Ngưỡng bếp bắt đầu hoạt động có nhiệt (T_active)
#define CONFIG_TEMP_WARN_C                  95.0f   // Ngưỡng nhiệt độ cảnh báo nguy cơ quá nhiệt (T_warn)
#define CONFIG_TEMP_WARN_RECOVER_C          80.0f   // Ngưỡng phục hồi hạ mức từ WARNING (Hysteresis 15°C)
#define CONFIG_TEMP_EMERGENCY_C             140.0f  // Ngưỡng nhiệt độ khẩn cấp (cháy khét đáy nồi, nguy cơ bắt lửa)
#define CONFIG_TEMP_EMERGENCY_RECOVER_C     110.0f  // Ngưỡng phục hồi hạ mức từ EMERGENCY (Hysteresis 30°C)

// -----------------------------------------------------------------------------
// 2. NGƯỠNG ĐẠO HÀM BIẾN THIÊN NHIỆT ĐỘ dT/dt (°C/giây)
// -----------------------------------------------------------------------------
#define CONFIG_DT_NOISE_THRESHOLD_CPS       0.5f    // Ngưỡng vượt mức nhiễu để ghi nhận nhiệt độ đang tăng
#define CONFIG_DT_WARN_THRESHOLD_CPS        2.5f    // Tốc độ tăng nhiệt độ đột biến bất thường (bắt lửa/cháy bùng)

// -----------------------------------------------------------------------------
// 3. THỜI GIAN DUY TRÌ & XÁC NHẬN TRẠNG THÁI (HOLD TIMES - GIÂY)
// -----------------------------------------------------------------------------
#define CONFIG_HOLD_NO_PRESENCE_SUSPICIOUS_SEC   60  // Thời gian vắng người khi bếp đang nóng để vào SUSPICIOUS (giây)
#define CONFIG_HOLD_WARNING_CONFIRM_SEC           3   // Thời gian duy trì điều kiện nguy hiểm để kích hoạt WARNING (giây)
#define CONFIG_HOLD_RECOVERY_CONFIRM_SEC          5   // Thời gian điều kiện an toàn liên tục trước khi hạ trạng thái (giây)
#define CONFIG_LONG_PRESS_ACK_SEC                 3   // Thời gian giữ nút TTP223 (Long-Press) để xác nhận giải tỏa EMERGENCY (giây)

// -----------------------------------------------------------------------------
// 4. NGƯỠNG KHÍ GAS / KHÓI MQ-2
// -----------------------------------------------------------------------------
#define CONFIG_MQ2_WARN_RAW_ADC             800     // Ngưỡng ADC phát hiện khí gas/khói nhạy (clean air ~300..500)
#define CONFIG_MQ2_WARMUP_TIME_SEC          15      // Thời gian sấy warm-up: đếm ngược 15s để cảm biến ổn định nhiệt độ nung

// -----------------------------------------------------------------------------
// 5. CẤU HÌNH CẢNH BÁO CỤC BỘ & MUTE TẠI CHỖ (LOCAL ALARM & MUTE)
// -----------------------------------------------------------------------------
#define CONFIG_MUTE_DURATION_SEC            60      // Thời gian tạm tắt còi (Mute) khi đang WARNING (giây)
#define CONFIG_BUZZER_WARN_ON_MS            300     // Thời gian bật còi trong 1 chu kỳ ngắt quãng WARNING (ms)
#define CONFIG_BUZZER_WARN_OFF_MS           400     // Thời gian ngắt còi trong 1 chu kỳ ngắt quãng WARNING (ms)
#define CONFIG_BUZZER_FAULT_ON_MS           100     // Thời gian bíp ngắn báo lỗi cảm biến (ms)
#define CONFIG_BUZZER_FAULT_PERIOD_MS       5000    // Chu kỳ bíp nhắc lỗi cảm biến khi ở FAULT (ms)
