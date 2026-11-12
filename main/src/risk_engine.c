#include "risk_engine.h"
#include "latch_store.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "RISK_ENGINE";

// Trạng thái nội bộ của Risk Engine
static risk_level_t s_current_risk_level = RISK_UNKNOWN;
static monitoring_status_t s_current_monitoring_status = MONITORING_FAULT;
static risk_reason_t s_current_reason = REASON_INITIALIZING;

static bool s_emergency_latched = false;
static bool s_emergency_acknowledged = false;

// Monotonic timestamp phục vụ bộ đếm xác nhận phục hồi an toàn (Hysteresis Hold)
static int64_t s_recovery_start_us = 0;
static bool s_long_press_triggered = false;
static bool s_ack_eligible, s_clear_pending, s_set_dirty;
static uint32_t s_physical_ack_seq, s_pending_generation;
static detection_state_t s_detection;
/* Runtime state is owned exclusively by the safety task. */
void risk_engine_persistence_step(void)
{
    if(s_set_dirty){
        if(latch_store_request(true,s_physical_ack_seq,&s_pending_generation)==ESP_OK)s_set_dirty=false;
    }else if(s_clear_pending && s_pending_generation==0){
        if(latch_store_request(false,s_physical_ack_seq+1,&s_pending_generation)!=ESP_OK){
            s_clear_pending=false;s_emergency_acknowledged=false;
        }
    }
}

esp_err_t risk_engine_init(void)
{
    s_current_risk_level = RISK_UNKNOWN;
    s_current_monitoring_status = MONITORING_FAULT;
    s_current_reason = REASON_INITIALIZING;
    latch_store_status_t stored=latch_store_status();
    s_emergency_latched=stored.latched || stored.health!=LATCH_STORE_DURABLE;
    s_physical_ack_seq=stored.ack_seq;
    s_clear_pending=false;s_ack_eligible=false;s_pending_generation=0;
    s_set_dirty=s_emergency_latched && stored.health!=LATCH_STORE_DURABLE;
    s_detection=DETECTION_UNKNOWN;
    if(s_emergency_latched)s_current_risk_level=RISK_EMERGENCY;
    s_emergency_acknowledged = false;
    s_recovery_start_us = 0;
    s_long_press_triggered = false;

    ESP_LOGI(TAG,"Risk initialized: restored_latch=%d physical_ack_seq=%lu persistence=%d",s_emergency_latched,(unsigned long)s_physical_ack_seq,stored.health);
    return ESP_OK;
}

const char* risk_level_to_str(risk_level_t level)
{
    switch (level) {
    case RISK_UNKNOWN:    return "UNKNOWN";
    case RISK_NORMAL:     return "NORMAL";
    case RISK_SUSPICIOUS: return "SUSPICIOUS";
    case RISK_WARNING:    return "WARNING";
    case RISK_EMERGENCY:  return "EMERGENCY";
    default:              return "UNDEFINED";
    }
}

const char* monitoring_status_to_str(monitoring_status_t status)
{
    switch (status) {
    case MONITORING_OK:       return "OK";
    case MONITORING_DEGRADED: return "DEGRADED";
    case MONITORING_FAULT:    return "FAULT";
    default:                  return "UNDEFINED";
    }
}

const char* risk_reason_to_str(risk_reason_t reason)
{
    switch (reason) {
    case REASON_NONE:                   return "NONE";
    case REASON_INITIALIZING:           return "INITIALIZING";
    case REASON_FIRE_DETECTED:          return "FIRE_DETECTED";
    case REASON_CRITICAL_TEMPERATURE:   return "CRITICAL_TEMPERATURE";
    case REASON_HIGH_TEMPERATURE:       return "HIGH_TEMPERATURE";
    case REASON_RAPID_TEMPERATURE_RISE: return "RAPID_TEMPERATURE_RISE";
    case REASON_UNATTENDED_COOKING:     return "UNATTENDED_COOKING";
    case REASON_GAS_SMOKE_ELEVATED:     return "GAS_SMOKE_ELEVATED";
    case REASON_SENSOR_FAULT:           return "SENSOR_FAULT";
    case REASON_SENSOR_STALE:           return "SENSOR_STALE";
    case REASON_RECOVERY_PENDING:       return "RECOVERY_PENDING";
    case REASON_EMERGENCY_LATCHED:      return "EMERGENCY_LATCHED";
    case REASON_EMERGENCY_ACKNOWLEDGED: return "EMERGENCY_ACKNOWLEDGED";
    case REASON_NORMAL_SAFE:            return "NORMAL_SAFE";
    default:                            return "UNKNOWN_REASON";
    }
}

/**
 * @brief Đánh giá độc lập trạng thái sẵn sàng của hệ thống cảm biến (Monitoring Status)
 */
static monitoring_status_t evaluate_monitoring_status(const kitchen_sensor_data_t *snapshot)
{
    // Cảm biến GY-906 là cảm biến nhiệt độ bắt buộc cốt lõi. Mất GY-906 là FAULT ngay lập tức.
    if (snapshot->gy906_quality != QUALITY_VALID) {
        return MONITORING_FAULT;
    }

    // Nếu mất đồng thời cả cảm biến lửa và radar
    if (snapshot->flame_quality != QUALITY_VALID && snapshot->radar_quality != QUALITY_VALID) {
        return MONITORING_FAULT;
    }

    // Nếu mất một vài cảm biến phụ hoặc MQ-2 đang warm-up nhưng vẫn chạy được ít nhất 1 luật an toàn
    if (snapshot->flame_quality != QUALITY_VALID ||
        snapshot->mq2_quality != QUALITY_VALID ||
        snapshot->radar_quality != QUALITY_VALID ||
        snapshot->mq2_warming_up) {
        return MONITORING_DEGRADED;
    }

    return MONITORING_OK;
}

static bool acknowledge_physical_hold(void)
{
    if(s_clear_pending && latch_store_status().health==LATCH_STORE_FAULT){
        /* A new physical hold may explicitly retry a failed asynchronous clear. */
        s_clear_pending=false;s_pending_generation=0;
    }
    if (!s_emergency_latched || !s_ack_eligible || s_clear_pending || s_physical_ack_seq==UINT32_MAX) {
        return false;
    }

    s_emergency_acknowledged = true;
    ESP_LOGI(TAG, "Da nhan lenh xac nhan giai toa EMERGENCY tai cho!");
    return true;
}

bool risk_engine_process_touch_hold(uint32_t touch_hold_time_ms)
{
    if (touch_hold_time_ms >= (CONFIG_LONG_PRESS_ACK_SEC * 1000)) {
        if (!s_long_press_triggered) {
            s_long_press_triggered = true;
            ESP_LOGW(TAG, "Phat hien Long-Press TTP223 (>= %d giay) -> Kich hoat Acknowledge!", CONFIG_LONG_PRESS_ACK_SEC);
            return acknowledge_physical_hold();
        }
    } else {
        s_long_press_triggered = false;
    }
    return false;
}

static void evaluate(const kitchen_sensor_data_t *snapshot, risk_state_t *out_state)
{
    if (snapshot == NULL || out_state == NULL) {
        return;
    }

    int64_t now = esp_timer_get_time();

    // 1. Đánh giá Monitoring Status hoàn toàn độc lập với Risk Level
    s_current_monitoring_status = evaluate_monitoring_status(snapshot);

    // -------------------------------------------------------------------------
    // 2. KIỂM TRA ĐIỀU KIỆN KHẨN CẤP (EMERGENCY) - QUYỀN ƯU TIÊN CAO NHẤT
    // Nhảy thẳng lên EMERGENCY từ bất kỳ trạng thái nào mà không cần qua tuần tự
    // -------------------------------------------------------------------------
    bool condition_fire = (snapshot->flame_quality == QUALITY_VALID &&
                           snapshot->flame_state == FLAME_STATE_FIRE);

    bool condition_critical_temp = (snapshot->gy906_quality == QUALITY_VALID &&
                                    snapshot->temp_object_filtered >= CONFIG_TEMP_EMERGENCY_C);

    s_detection=condition_fire||condition_critical_temp?DETECTION_EMERGENCY_PRESENT:
        snapshot->gy906_quality==QUALITY_VALID && snapshot->flame_quality==QUALITY_VALID?
        DETECTION_NO_EMERGENCY:DETECTION_UNKNOWN;

    if (condition_fire || condition_critical_temp) {
        s_current_risk_level = RISK_EMERGENCY;
        if(!s_emergency_latched || s_clear_pending){s_set_dirty=true;s_pending_generation=0;}
        s_clear_pending=false;
        s_emergency_latched = true;
        s_emergency_acknowledged = false;
        s_recovery_start_us = 0;

        if (condition_fire) {
            s_current_reason = REASON_FIRE_DETECTED;
        } else {
            s_current_reason = REASON_CRITICAL_TEMPERATURE;
        }

        out_state->risk_level = s_current_risk_level;
        out_state->monitoring_status = s_current_monitoring_status;
        out_state->risk_reason = s_current_reason;
        out_state->emergency_latched = s_emergency_latched;
        out_state->emergency_acknowledged = s_emergency_acknowledged;
        out_state->recovery_countdown_sec = 0;
        out_state->last_eval_time_us = now;
        return;
    }

    // -------------------------------------------------------------------------
    // 3. XỬ LÝ TRƯỜNG HỢP ĐANG BỊ CHỐT KHẨN CẤP (EMERGENCY LATCHED)
    // Quy tắc: Chỉ được xóa khi điều kiện nguy hiểm đã phục hồi VÀ có xác nhận tại chỗ
    // -------------------------------------------------------------------------
    if (s_emergency_latched) {
        // Kiểm tra điều kiện vật lý đã thực sự hạ nhiệt và an toàn chưa
        bool temp_is_safe_now = (snapshot->gy906_quality == QUALITY_VALID &&
                                 snapshot->temp_object_filtered < CONFIG_TEMP_EMERGENCY_RECOVER_C);
        bool flame_is_safe_now = (snapshot->flame_quality == QUALITY_VALID &&
                                  snapshot->flame_state == FLAME_STATE_SAFE);

        s_ack_eligible=temp_is_safe_now && flame_is_safe_now && latch_store_status().available;
        if (temp_is_safe_now && flame_is_safe_now) {
            // Điều kiện vật lý đã an toàn, nhưng phải chờ xác nhận tại chỗ
            latch_store_status_t durable=latch_store_status();
            bool clear_done=s_clear_pending && s_pending_generation!=0 &&
                durable.health==LATCH_STORE_DURABLE && !durable.latched &&
                durable.committed_generation==s_pending_generation;
            if (clear_done) {
                s_emergency_latched=false;s_clear_pending=false;
                s_physical_ack_seq=durable.ack_seq;
                s_emergency_acknowledged=false;s_pending_generation=0;
                s_current_reason=REASON_EMERGENCY_ACKNOWLEDGED;
                s_recovery_start_us=now;
                ESP_LOGI(TAG,"Physical ACK applied durably; latch cleared ack_seq=%lu",(unsigned long)s_physical_ack_seq);
            } else {
                if(s_emergency_acknowledged && !s_clear_pending){
                    s_clear_pending=true;s_pending_generation=0;
                }
                s_emergency_acknowledged=false;
                // Await durable clear; alarm remains continuous.
                s_current_risk_level = RISK_EMERGENCY;
                s_current_reason = REASON_EMERGENCY_LATCHED;

                out_state->risk_level = s_current_risk_level;
                out_state->monitoring_status = s_current_monitoring_status;
                out_state->risk_reason = s_current_reason;
                out_state->emergency_latched = true;
                out_state->emergency_acknowledged = false;
                out_state->recovery_countdown_sec = 0;
                out_state->last_eval_time_us = now;
                return;
            }
        } else {
            if(s_clear_pending){s_set_dirty=true;s_clear_pending=false;s_pending_generation=0;}
            // Điều kiện nguy hiểm vẫn còn tồn tại -> Không cho phép giải tỏa kể cả bấm nhầm nút
            s_emergency_acknowledged = false;
            s_current_risk_level = RISK_EMERGENCY;

            out_state->risk_level = s_current_risk_level;
            out_state->monitoring_status = s_current_monitoring_status;
            out_state->risk_reason = s_current_reason;
            out_state->emergency_latched = true;
            out_state->emergency_acknowledged = false;
            out_state->recovery_countdown_sec = 0;
            out_state->last_eval_time_us = now;
            return;
        }
    }

    // -------------------------------------------------------------------------
    // 4. KIỂM TRA ĐIỀU KIỆN CẢNH BÁO (WARNING)
    // Kích hoạt khi:
    //  - Nhiệt độ >= T_warn (Quá nhiệt)
    //  - dT/dt cao KẾT HỢP MQ-2 tăng (Nhiệt tăng vọt bốc khói)
    //  - HOẶC Rò rỉ khí gas/khói MQ-2 đơn lẻ (chân DO kéo xuống hoặc ADC >= ngưỡng)
    // -------------------------------------------------------------------------
    bool condition_high_temp = (snapshot->gy906_quality == QUALITY_VALID &&
                                snapshot->temp_object_filtered >= CONFIG_TEMP_WARN_C);

    bool condition_fast_rise_with_gas = (snapshot->gy906_quality == QUALITY_VALID &&
                                         snapshot->dT_dt_valid &&
                                         snapshot->dT_dt >= CONFIG_DT_WARN_THRESHOLD_CPS &&
                                         (snapshot->mq2_gas_detected || snapshot->mq2_raw_filtered >= CONFIG_MQ2_WARN_RAW_ADC));

    bool condition_gas_leak = (snapshot->mq2_quality == QUALITY_VALID &&
                               (snapshot->mq2_gas_detected || snapshot->mq2_raw_filtered >= CONFIG_MQ2_WARN_RAW_ADC) &&
                               (snapshot->t_hold_gas_ms >= (CONFIG_HOLD_WARNING_CONFIRM_SEC * 1000)));

    if (condition_high_temp || condition_fast_rise_with_gas || condition_gas_leak) {
        s_current_risk_level = RISK_WARNING;
        s_recovery_start_us = 0;

        if (condition_high_temp) {
            s_current_reason = REASON_HIGH_TEMPERATURE;
        } else if (condition_fast_rise_with_gas) {
            s_current_reason = REASON_RAPID_TEMPERATURE_RISE;
        } else {
            s_current_reason = REASON_GAS_SMOKE_ELEVATED;
        }

        out_state->risk_level = s_current_risk_level;
        out_state->monitoring_status = s_current_monitoring_status;
        out_state->risk_reason = s_current_reason;
        out_state->emergency_latched = false;
        out_state->emergency_acknowledged = false;
        out_state->recovery_countdown_sec = 0;
        out_state->last_eval_time_us = now;
        return;
    }

    // -------------------------------------------------------------------------
    // 5. KIỂM TRA ĐIỀU KIỆN NGHI NGỜ (SUSPICIOUS - UNATTENDED COOKING)
    // Bếp đang nóng/đun nấu (T > T_active hoặc dT/dt > 0) nhưng vắng người kéo dài
    // Quy tắc: Không được coi radar mất kết nối là không có người!
    // -------------------------------------------------------------------------
    bool stove_is_active = (snapshot->gy906_quality == QUALITY_VALID &&
                           (snapshot->temp_object_filtered >= CONFIG_TEMP_ACTIVE_C ||
                           (snapshot->dT_dt_valid && snapshot->dT_dt >= CONFIG_DT_NOISE_THRESHOLD_CPS)));

    // Bắt buộc Radar phải VALID mới được kết luận không có người
    // Tuyệt đối không được coi radar mất kết nối là không có người!
    bool no_presence_confirmed = (snapshot->radar_quality == QUALITY_VALID &&
                                  snapshot->radar_presence == 0);

    bool unattended_timeout = (snapshot->t_hold_no_presence_ms >= (CONFIG_HOLD_NO_PRESENCE_SUSPICIOUS_SEC * 1000));

    if (stove_is_active && no_presence_confirmed && unattended_timeout) {
        s_current_risk_level = RISK_SUSPICIOUS;
        s_current_reason = REASON_UNATTENDED_COOKING;
        s_recovery_start_us = 0;

        out_state->risk_level = s_current_risk_level;
        out_state->monitoring_status = s_current_monitoring_status;
        out_state->risk_reason = s_current_reason;
        out_state->emergency_latched = false;
        out_state->emergency_acknowledged = false;
        out_state->recovery_countdown_sec = 0;
        out_state->last_eval_time_us = now;
        return;
    }

    // -------------------------------------------------------------------------
    // 6. QUY TẮC KHÔNG TỰ HẠ MỨC KHI CẢM BIẾN LỖI HOẶC STALE
    // Nếu đang ở WARNING hoặc SUSPICIOUS mà cảm biến bị lỗi/mất tín hiệu -> Giữ nguyên mức nguy cơ
    // -------------------------------------------------------------------------
    if (s_current_risk_level == RISK_WARNING || s_current_risk_level == RISK_SUSPICIOUS) {
        if (snapshot->gy906_quality == QUALITY_INVALID || snapshot->flame_quality == QUALITY_INVALID) {
            s_recovery_start_us = 0;
            s_current_reason = REASON_SENSOR_FAULT;
            out_state->risk_level = s_current_risk_level;
            out_state->monitoring_status = s_current_monitoring_status;
            out_state->risk_reason = s_current_reason;
            out_state->emergency_latched = false;
            out_state->emergency_acknowledged = false;
            out_state->recovery_countdown_sec = 0;
            out_state->last_eval_time_us = now;
            return;
        }

        if (snapshot->gy906_quality == QUALITY_STALE || snapshot->flame_quality == QUALITY_STALE) {
            s_recovery_start_us = 0;
            s_current_reason = REASON_SENSOR_STALE;
            out_state->risk_level = s_current_risk_level;
            out_state->monitoring_status = s_current_monitoring_status;
            out_state->risk_reason = s_current_reason;
            out_state->emergency_latched = false;
            out_state->emergency_acknowledged = false;
            out_state->recovery_countdown_sec = 0;
            out_state->last_eval_time_us = now;
            return;
        }
    }

    // -------------------------------------------------------------------------
    // 7. KIỂM TRA ĐIỀU KIỆN PHỤC HỒI AN TOÀN (NORMAL RECOVERY WITH HYSTERESIS)
    // Chỉ được về NORMAL khi cảm biến hợp lệ, nhiệt độ < T_recover, không có nguy cơ,
    // và duy trì an toàn liên tục đủ thời gian CONFIG_HOLD_RECOVERY_CONFIRM_SEC (5s)
    // -------------------------------------------------------------------------
    bool all_mandatory_valid = (snapshot->gy906_quality == QUALITY_VALID &&
                                snapshot->flame_quality == QUALITY_VALID &&
                                !snapshot->mq2_warming_up);

    bool physical_safe = (snapshot->temp_object_filtered < CONFIG_TEMP_WARN_RECOVER_C &&
                          snapshot->flame_state == FLAME_STATE_SAFE &&
                          !snapshot->mq2_gas_detected &&
                          snapshot->mq2_raw_filtered < CONFIG_MQ2_WARN_RAW_ADC);

    if (all_mandatory_valid && physical_safe) {
        // Nếu trước đó đang ở mức nguy cơ cao (SUSPICIOUS hoặc WARNING), kích hoạt đếm ngược phục hồi
        if (s_current_risk_level == RISK_WARNING || s_current_risk_level == RISK_SUSPICIOUS) {
            if (s_recovery_start_us == 0) {
                s_recovery_start_us = now;
            }

            int64_t elapsed_recovery_us = now - s_recovery_start_us;
            int64_t target_recovery_us = (CONFIG_HOLD_RECOVERY_CONFIRM_SEC * 1000000LL);

            if (elapsed_recovery_us < target_recovery_us) {
                // Đang trong thời gian chờ kiểm chứng an toàn liên tục
                s_current_reason = REASON_RECOVERY_PENDING;
                uint32_t remaining_sec = (uint32_t)((target_recovery_us - elapsed_recovery_us) / 1000000LL) + 1;

                out_state->risk_level = s_current_risk_level;
                out_state->monitoring_status = s_current_monitoring_status;
                out_state->risk_reason = s_current_reason;
                out_state->emergency_latched = false;
                out_state->emergency_acknowledged = false;
                out_state->recovery_countdown_sec = remaining_sec;
                out_state->last_eval_time_us = now;
                return;
            }
        }

        // Đã hoàn thành thời gian kiểm chứng an toàn -> Chuyển về NORMAL
        s_current_risk_level = RISK_NORMAL;
        s_current_reason = REASON_NORMAL_SAFE;
        s_recovery_start_us = 0;
    } else {
        s_recovery_start_us = 0;
        // Cảm biến bắt buộc không hợp lệ, hoặc đang warm-up MQ-2, hoặc chưa an toàn vật lý
        if (snapshot->mq2_warming_up) {
            if (s_current_risk_level != RISK_WARNING && s_current_risk_level != RISK_EMERGENCY) {
                s_current_risk_level = RISK_UNKNOWN;
                s_current_reason = REASON_INITIALIZING;
            }
        } else if (!all_mandatory_valid) {
            // Mất cảm biến bắt buộc (GY-906 hoặc Flame): TUYỆT ĐỐI KHÔNG ĐƯỢC Ở NORMAL HOẶC VỀ NORMAL!
            // Nếu trước đó là NORMAL hoặc UNKNOWN -> Buộc chuyển sang UNKNOWN kèm lý do SENSOR_FAULT/STALE
            if (s_current_risk_level == RISK_NORMAL || s_current_risk_level == RISK_UNKNOWN) {
                s_current_risk_level = RISK_UNKNOWN;
                if (snapshot->gy906_quality == QUALITY_STALE || snapshot->flame_quality == QUALITY_STALE) {
                    s_current_reason = REASON_SENSOR_STALE;
                } else {
                    s_current_reason = REASON_SENSOR_FAULT;
                }
            }
        }
    }

    out_state->risk_level = s_current_risk_level;
    out_state->monitoring_status = s_current_monitoring_status;
    out_state->risk_reason = s_current_reason;
    out_state->emergency_latched = s_emergency_latched;
    out_state->emergency_acknowledged = s_emergency_acknowledged;
    out_state->recovery_countdown_sec = 0;
    out_state->last_eval_time_us = now;
}

void risk_engine_step(const kitchen_sensor_data_t *snapshot,risk_state_t *out_state)
{
    if(!snapshot || !out_state)return;
    s_ack_eligible=false;
    evaluate(snapshot,out_state);
    out_state->detection_state=s_detection;
    out_state->ack_eligible=s_emergency_latched && s_ack_eligible && !s_clear_pending;
    out_state->physical_ack_seq=s_physical_ack_seq;
    out_state->persistence_health=latch_store_status().health;
}
