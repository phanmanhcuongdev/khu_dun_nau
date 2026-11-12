# Kiến trúc firmware

[← Mục lục](../README.md) · Phạm vi: đường giám sát cục bộ, tác vụ mạng và lưu trạng thái.

## Khởi động và vòng giám sát

[app_main](../../main/main.c) khởi tạo buzzer trước persistence, sau đó NVS/latch store, nút, cảm biến, OLED, pipeline, risk engine và alarm controller. Lỗi một số ngoại vi được ghi nhận để phần còn lại tiếp tục; không xóa NVS nhằm che lỗi khởi tạo. Latch đã lưu hoặc storage không durable dẫn tới xử lý bảo thủ và còi liên tục theo code khởi động.

Mỗi lượt main loop xử lý theo thứ tự:

1. Thu nhận snapshot, lọc và cập nhật quality/hold times trong sensor pipeline.
2. Đánh giá risk cùng monitoring status.
3. Ánh xạ risk/monitoring thành trạng thái RGB.
4. Xử lý touch, mute, còi và OLED.
5. Ghi số đo timing, gửi yêu cầu persistence của risk engine.
6. Chuyển snapshot tới network task bằng queue phi chặn.
7. Log tóm tắt và `vTaskDelay` 1000 ms.

1000 ms là khoảng delay danh nghĩa; chu kỳ thực gồm cả thời gian xử lý. Các giá trị timing đo được không phải cam kết worst-case sensor-to-alarm latency.

## Lọc và chất lượng dữ liệu

Pipeline có cửa sổ 3 mẫu: nhiệt độ dùng median khi cửa sổ đã đầy, MQ-2 dùng trung bình. dT/dt chỉ được sử dụng khi cờ validity cho phép; khi mẫu nhiệt không hợp lệ/stale, pipeline hủy cờ và reset mốc liên quan.

| Cảm biến | Timeout STALE cấu hình |
|---|---|
| GY-906 | 3000 ms |
| MQ-2 | 3000 ms |
| Flame | 2500 ms |
| Radar | 3000 ms |
| Touch | 2000 ms |

Các giá trị lấy từ [sensor_pipeline.h](../../main/include/sensor_pipeline.h); diễn giải invalid/stale còn phụ thuộc từng nhánh thu nhận trong pipeline. Không dùng riêng timeout để suy độ trễ phát hiện lỗi đứt dây. Quality từng cảm biến có `INITIALIZING`, `VALID`, `INVALID`, `STALE`, tách với monitoring status và overall quality.

## Module và trách nhiệm

| Nhóm | File/module | Trách nhiệm |
|---|---|---|
| Drivers | `gy906_sensor`, `mq2_sensor`, `flame_sensor`, `ld2410_radar`, `adc_shared`, `touch_btn` | Truy cập cảm biến và tài nguyên phần cứng |
| Pipeline | `sensor_pipeline`, `sensor_types` | Chất lượng dữ liệu, lọc, dT/dt, thời gian duy trì |
| Luật | `risk_engine`, `safety_config` | Risk/monitoring, điều kiện khẩn cấp, hysteresis và điều kiện ACK |
| Alarm/UI | `local_alarm_controller`, `buzzer`, `oled_display`, `oled_ui` | Điều phối đầu ra theo kết quả risk |
| RGB | `components/rgb_status` | Mẫu LED độc lập với việc phân loại rủi ro |
| Latch | `latch_store` | Lưu chốt khẩn cấp/physical ACK bền qua NVS |
| Record | `cloud_record`, `cloud_codec` | Chụp dữ liệu, identity và serialize contract V1 |
| Queue | `event_buffer` | Sự kiện RAM hữu hạn, claim/retry/complete theo identity |
| Delivery | `mqtt_delivery` | Ghép epoch + MID + token với PUBACK |
| Journal | `critical_journal` | Bản sao critical records hữu hạn trong NVS |
| Network | `wifi_app`, `tb_mqtt`, `publish_policy`, `clock_policy` | Kết nối, clock trust, lịch gửi, reconnect và retry |

## Quyền sở hữu và tính độc lập

Safety task giữ trạng thái runtime của risk engine. Nó không chờ socket MQTT hoặc PUBACK; snapshot được chuyển bất đồng bộ. Network task sở hữu vòng đời MQTT, Wi-Fi retry polling và event journal. Latch store có đường yêu cầu/commit riêng; clear chỉ được áp dụng sau khi xác nhận ghi bền đúng generation.

Đường local safety có thể tiếp tục khi thiếu provisioning, mất mạng hoặc MQTT không khởi tạo được. Điều này không có nghĩa mọi lỗi phần cứng đều được phát hiện hay mọi đầu ra đều còn hoạt động.

## Lưu trữ: hai mục đích riêng

| Lưu trữ | Dữ liệu | Giới hạn/ý nghĩa |
|---|---|---|
| Durable latch | Emergency latch và physical ACK sequence | Khôi phục alarm qua reboot; không bị Cloud ACK xóa |
| Event RAM | Tối đa 32 records | Ưu tiên emergency, warning, fault, info; hữu hạn, có counters |
| Delivery ledger | Tối đa 4 in-flight records | Ghép PUBACK đúng session/record, timeout thì retry |
| Critical journal | 4 slots NVS | Bản sao critical history; không là kho mọi telemetry |

Identity lịch sử gồm boot ID và sequence; retry/replay giữ ý nghĩa quan sát gốc. PUBACK cho biết broker xác nhận transport QoS1, không chứng minh database/Rule Chain đã xử lý. RAM đầy, mất nguồn trước khi commit, hoặc journal lỗi/đầy có thể tạo history gaps. Đọc counters và tài liệu contract để hiểu phạm vi mất dữ liệu.

## Cấu hình và build identity

- [board_pins.h](../../main/include/board_pins.h): sơ đồ chân và bus.
- [safety_config.h](../../main/include/safety_config.h): ngưỡng, hold/mute và buzzer timing.
- [diagnostic_config.h](../../main/include/diagnostic_config.h): compile-time diagnostics.
- [tb_mqtt.h](../../main/include/tb_mqtt.h): broker URI và nhãn node/zone.
- [sdkconfig](../../sdkconfig): cấu hình ESP-IDF và component Kconfig.
- [generate_build_config.py](../../tools/generate_build_config.py): sinh private header, firmware/build/config identity; không đưa credential values vào public fingerprints.

Báo cáo lịch sử: [WP01](../production/wp01-implementation-report.md), [Phase 1](../production/phase1-implementation-report.md). Tích hợp dữ liệu: [ThingsBoard](thingsboard.md).
