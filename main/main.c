#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "latch_store.h"

#include "board_pins.h"
#include "sensor_types.h"
#include "sensor_pipeline.h"
#include "risk_engine.h"
#include "local_alarm_controller.h"
#include "event_buffer.h"
#include "flame_sensor.h"
#include "mq2_sensor.h"
#include "gy906_sensor.h"
#include "oled_display.h"
#include "ld2410_radar.h"
#include "buzzer.h"
#include "touch_btn.h"
#include "wifi_app.h"
#include "tb_mqtt.h"
#include "rgb_status.h"
#ifdef CONFIG_RGB_STATUS_SELF_TEST
#include "rgb_self_test.h"
#endif

static const char *TAG = "APP_MAIN";

void app_main(void)
{
#ifdef CONFIG_RGB_STATUS_WIRING_CONFIRMED
_Static_assert(CONFIG_RGB_STATUS_R_GPIO == 39 && CONFIG_RGB_STATUS_G_GPIO == 38 &&
               CONFIG_RGB_STATUS_B_GPIO == 48,
               "Board integration requires a new pin conflict audit before changing RGB pins");
    if (rgb_status_init(CONFIG_RGB_STATUS_R_GPIO, CONFIG_RGB_STATUS_G_GPIO,
                        CONFIG_RGB_STATUS_B_GPIO) != ESP_OK) {
        ESP_LOGE(TAG, "RGB initialization failed; monitoring continues");
    } else {
        ESP_LOGI(TAG, "RGB initialized R39/G38/B48 LEDC timer3 channels5/6/7");
    }
#endif
    /* Buzzer driver before persistence; never erase NVS to hide storage errors. */
    if(buzzer_init()!=ESP_OK)ESP_LOGE(TAG,"Buzzer init failed");
    esp_err_t ret=nvs_flash_init();
    if(ret!=ESP_OK)ESP_LOGE(TAG,"NVS init failed: %s; no erase, conservative latch",esp_err_to_name(ret));
    esp_err_t store_ret=latch_store_init(ret);
    if(store_ret!=ESP_OK)ESP_LOGE(TAG,"Latch persistence unavailable: %s",esp_err_to_name(store_ret));
    if(latch_store_status().latched || latch_store_status().health!=LATCH_STORE_DURABLE)buzzer_set_mode(BUZZER_MODE_CONTINUOUS);
    if (touch_btn_init() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Touch TTP223!");
    }

    // Khởi tạo các cảm biến độc lập - Không dùng ESP_ERROR_CHECK để tránh panic/reboot khi thiếu cảm biến
    if (flame_sensor_init() != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao Flame Sensor that bai (He thong van tiep tuc)");
    }
    if (mq2_sensor_init() != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao MQ-2 that bai (He thong van tiep tuc)");
    }
    if (gy906_sensor_init() != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao GY-906 that bai! (He thong se vao che do FAULT)");
    }
    if (oled_display_init() != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao OLED SSD1306 that bai (Tiep tuc khong co man hinh)");
    }
    if (ld2410_radar_init() != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao LD2410 Radar that bai (He thong van tiep tuc)");
    }

    // Khởi tạo Sensor Pipeline, Edge Risk Engine và Local Alarm Controller
    if (sensor_pipeline_init() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Sensor Pipeline!");
    }
    if (risk_engine_init() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Risk Engine!");
    }
    if (local_alarm_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Local Alarm Controller!");
    }

    // Bíp khởi động ngắn 100ms báo hiệu phần cứng đã sẵn sàng
    if(!latch_store_status().latched && latch_store_status().health==LATCH_STORE_DURABLE)buzzer_beep(100);

    // Khoi tao Wi-Fi; ket noi/retry do network task so huu, khong doi online.
    ESP_LOGI(TAG, "Khoi tao Wi-Fi STA...");
    if(ret==ESP_OK){if(wifi_init_sta()!=ESP_OK)ESP_LOGE(TAG,"Wi-Fi init unavailable; local safety continues");}
    else ESP_LOGE(TAG,"Skipping cloud startup: NVS unavailable; local safety continues");

    // Khởi tạo ThingsBoard MQTTS Client & Network Task độc lập
    ESP_LOGI(TAG, "Khoi tao ThingsBoard MQTT Client & Network Task...");
    if(tb_mqtt_init()!=ESP_OK)ESP_LOGE(TAG,"Cloud task unavailable; local safety continues");

    // Trạng thái ban đầu: khởi tạo UNKNOWN/INITIALIZING
    kitchen_sensor_data_t sensor_data;
    memset(&sensor_data, 0, sizeof(sensor_data));

    local_alarm_status_t alarm_status;
    memset(&alarm_status, 0, sizeof(alarm_status));

    ESP_LOGI(TAG, "Bat dau chu ky giam sat va danh gia rui ro an toan (1000ms)...");

#ifdef CONFIG_RGB_STATUS_SELF_TEST
    rgb_self_test_t rgb_test = {0};
    int rgb_test_previous = -1;
#endif
    int64_t previous_step_start=0;
    while (1) {
        int64_t step_start=esp_timer_get_time();
        // 1. Thực thi toàn diện Pipeline cảm biến (Lấy mẫu, lọc số, Data Quality, dT/dt, t_hold)
        sensor_pipeline_step(&sensor_data);

        // 2. Thực thi Edge Risk Engine đánh giá độc lập Risk Level và Monitoring Status
        risk_engine_step(&sensor_data, &sensor_data.risk_state);

        rgb_status_t rgb_live = (sensor_data.risk_state.risk_level == RISK_EMERGENCY ? RGB_FIRE :
                       sensor_data.risk_state.monitoring_status == MONITORING_FAULT ? RGB_SENSOR_ERROR :
                       sensor_data.risk_state.risk_level == RISK_WARNING ? RGB_WARNING :
                       sensor_data.risk_state.risk_level == RISK_UNKNOWN ? RGB_BOOTING : RGB_INFO);
#ifdef CONFIG_RGB_STATUS_SELF_TEST
        rgb_status_t rgb_selected = rgb_self_test_select(&rgb_test, rgb_live, esp_timer_get_time());
        if (!rgb_test.finished && rgb_test_previous != (int)rgb_selected) {
            static const char *names[] = {"BOOTING", "INFO", "WARNING", "FIRE", "SENSOR_ERROR"};
            ESP_LOGI(TAG, "RGB LED-only self-test: %s", names[rgb_selected]);
            rgb_test_previous = (int)rgb_selected;
        } else if (rgb_test.finished && rgb_test_previous != -2) {
            ESP_LOGI(TAG, "RGB self-test ended/cancelled; Risk Engine controls LED");
            rgb_test_previous = -2;
        }
        rgb_status_set(rgb_selected);
#else
        rgb_status_set(rgb_live);
#endif

        // 4. Thực thi Local Alarm Controller: Điều phối còi, MUTE, Long-Press ACK, và OLED tại chỗ
        local_alarm_controller_step(&sensor_data,
                                    wifi_is_connected(),
                                    tb_mqtt_is_connected(),
                                    &alarm_status);

        tb_safety_timing(esp_timer_get_time()-step_start,previous_step_start?step_start-previous_step_start:0);
        previous_step_start=step_start;
        risk_engine_persistence_step();

        // Cập nhật trạng thái Mute lên struct dữ liệu
        sensor_data.is_muted = alarm_status.is_muted;
        sensor_data.mute_remaining_sec = alarm_status.mute_remaining_sec;

        // 5. Chuyển tiếp Telemetry sang Network Task qua Queue phi chặn (0ms delay)
        tb_send_telemetry_async(&sensor_data);

        ESP_LOGI(TAG, "Uptime:%us | T:%.1fC | MQ2 DO:%d (%s) | Flame DO:%d (%s) | Radar:%s | Risk:%s (Rsn:%s)",
                 (unsigned int)sensor_data.system_uptime_sec,
                 (double)sensor_data.temp_object_filtered,
                 sensor_data.mq2_do,
                 sensor_data.mq2_warming_up ? "WARMUP" : (sensor_data.mq2_gas_detected ? "GAS DETECTED!" : "Safe"),
                 sensor_data.flame_do,
                 (sensor_data.flame_state == FLAME_STATE_FIRE) ? "FIRE DETECTED!" : "Safe",
                 sensor_data.radar_presence ? "PERSON" : "EMPTY",
                 risk_level_to_str(sensor_data.risk_state.risk_level),
                 risk_reason_to_str(sensor_data.risk_state.risk_reason));

        vTaskDelay(pdMS_TO_TICKS(SAMPLING_INTERVAL_MS));
    }
}
