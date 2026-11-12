# PCCC V1 — MQTT Data Contract Handoff

[← Mục lục tài liệu](../README.md) · Chuẩn hóa tài liệu: **2026-11-12**.

**Trạng thái: tham chiếu contract firmware V1.** Đọc [hướng dẫn tích hợp](../guides/thingsboard.md) trước; các bước live Cloud chưa đánh dấu hoàn thành phải được nghiệm thu riêng trên tenant được cấp quyền.

Ngày bàn giao: **2026-11-11**. Firmware **1.1.0-phase1**; `schema_version=1`; ESP-IDF6.1 / ESP32-S3 N16R8. Đây là tài liệu mô hình dữ liệu **IMPLEMENTED** ở source hiện tại, không sửa firmware hoặc Cloud.

Bản build/flash có bằng chứng gần nhất: build_id `4dba43409b298ef3`, config_id `0e26394ea6944ef8`; các giá trị này là fingerprints của bản đã báo cáo, không hardcode để nhận diện mọi release sau. Device identity phía ThingsBoard lấy từ authenticated originator, không từ boot_id, MQTT client ID hoặc nhãn zone.

**Đọc trước khi tích hợp:** route bằng `record_kind`; historical TRANSITION không được ghi đè current state/clear Cloud Alarm. `emergency_latched`, `detection_state`, `ack_eligible`, `physical_ack_seq` là các khái niệm riêng. Cloud ACK không phải physical ACK. Missing measurement không phải zero và không tự xóa ThingsBoard Latest.

Các file đồng hành:

- [Telemetry dictionary CSV — UTF-8, comma-separated,93 data rows](PCCC_V1_TELEMETRY_DICTIONARY.csv).
- [Payload examples JSON —16 cases và provenance](PCCC_V1_PAYLOAD_EXAMPLES.json).
- [Cloud integration acceptance checklist](cloud-integration-checklist.md).

## Mục lục

1. [Phạm vi và trạng thái bằng chứng](#s1)
2. [MQTT connection contract](#s2)
3. [Wire format, identity và timestamp](#s3)
4. [Publish cadence](#s4)
5. [Telemetry dictionary đầy đủ](#s5)
6. [Enum dictionary](#s6)
7. [Client attributes](#s7)
8. [Payload examples](#s8)
9. [Event lifecycle và offline](#s9)
10. [Hướng dẫn Cloud routing](#s10)
11. [Alarm mapping](#s11)
12. [Dashboard handoff](#s12)
13. [Acceptance checklist](#s13)
14. [Known Discrepancies và giới hạn](#s14)
15. [Source references và validation](#s15)

<a id="s1"></a>
## 1. Phạm vi và trạng thái bằng chứng

| Nhãn | Nghĩa và áp dụng |
|---|---|
| IMPLEMENTED | Có trong publisher/serializer hiện tại; dictionary và wire format mục2–9 mô tả nhãn này |
| TESTED | Có evidence cụ thể; host/schema test khác với HIL hoặc Cloud DB ingestion |
| PROPOSED | Chưa triển khai; mọi routing/alarm/dashboard gợi ý ở mục10–13 ghi RECOMMENDED — NOT IMPLEMENTED |
| NOT VERIFIED | Chưa có bằng chứng; không suy từ MQTT PUBACK thành DB/Rule Chain PASS |

Evidence Phase1: host regression/schema fixtures và actual boot/TLS/MQTT/PUBACK đã TESTED. Boot capture180s có Wi-Fi IP, sane SNTP, CONNECTED và event PUBACK. Tenant identity/quota, Rule Chain, DB ingestion và dashboard **NOT VERIFIED**. User đã báo đã thử firmware trên thiết bị; tài liệu này không chạy lại hardware test hoặc fault injection.

Bộ bàn giao gồm tài liệu này, CSV93 dòng dữ liệu (50 current/health +34 history +9 attributes), JSON16 examples có provenance, và checklist riêng. `ts`/`values` là hai envelope members, **không** cộng vào telemetry-key count. `schema_version` có ở cả hai topic nhưng được tính riêng theo category. Trong 50 current/health keys có6 counters tên bắt đầu `event_`; chúng không là historical records.

<a id="s2"></a>
## 2. MQTT connection contract

| Thuộc tính | IMPLEMENTED |
|---|---|
| Broker | Host `iot.cuongdso.id.vn`, TCP port **8883**, URI `mqtts://iot.cuongdso.id.vn:8883` |
| Protocol | MQTT **3.1.1**: app không override protocol_ver; SDK default theo CONFIG_MQTT_PROTOCOL_311=y; MQTT5 không bật |
| TLS | Dùng embedded **ISRG Root X1** trong main/isrgrootx1.pem; giữ hostname verification mặc định và certificate-date verification enabled. Cần chain hợp lệ với trust anchor, hostname phù hợp và clock hợp lệ. Không cấu hình client certificate/mTLS, không bypass verification |
| Auth | Device access token dùng làm MQTT username từ private build configuration. Không cấu hình MQTT password. **Không chứa token trong tài liệu hoặc examples** |
| Client ID | App không gán explicit client_id; SDK tự sinh. Không dùng NODE_ID macro làm Cloud originator |
| Telemetry topic | `v1/devices/me/telemetry` |
| Client attributes topic | `v1/devices/me/attributes` |
| QoS/retain | **QoS1, retain=false** trên cả hai topics; enqueue store=true trong bounded client outbox |
| Session/keepalive | Clean session bật (`disable_clean_session=false`); keepalive30s. Keepalive protocol không phải telemetry heartbeat |
| Subscribe/RPC | **Không subscribe**, không RPC/shared-attribute listener hoặc remote safety command |
| Last Will | Không cấu hình LWT; không publish online=false khi mất kết nối |
| Offline | Không có socket gửi khi offline; bounded RAM/journal giữ history, local safety tiếp tục độc lập |
| Reconnect | Network task là owner; library auto-reconnect disabled; backoff1..60s + jitter dưới1s; timeout transport5s, initial connect attempt khoảng15s |
| On connect | Client attributes rồi latest full current resync (BOOT ở lần đầu, RECONNECT ở lần sau), sau đó history; đây là thứ tự enqueue, không là DB commit guarantee |

Clock bootstrap chỉ bắt đầu SNTP sau Wi-Fi có IP, servers pool.ntp.org/time.google.com, resync1h, retry60s khi chưa trust. MQTT chỉ khởi tạo nếu có provisioning, Wi-Fi và trusted clock. Sane range: build time minus1day tới trước build time plus5×366days; anchor age<24h, wall drift trong±5s so với monotonic projection. SNTP chuẩn chưa xác thực mật mã; đây là giới hạn trust, không phải authenticated time service.

Device token chỉ chứng minh cơ chế authentication device-side đang cấu hình, **không chứng minh có quyền quản trị tenant/Dashboard/Rule Chain**. Quyền/tenant mapping tại broker này NOT VERIFIED; không giả định endpoint là một tenant ThingsBoard Cloud cụ thể đã inspect. Online/offline Cloud là chức năng server cần cấu hình riêng, không có telemetry bool `online`/`wifi_connected`/`mqtt_connected`.

<a id="s3"></a>
## 3. Wire format, identity và timestamp

Khi capture đã có clock hợp lệ, MQTT body là native ThingsBoard envelope:

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "HEARTBEAT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "free_heap_bytes": 100000,
    "min_free_heap_bytes": 85000,
    "largest_free_block_bytes": 80000,
    "event_drop_count": 0,
    "event_expiry_count": 0,
    "event_reject_count": 0,
    "event_eviction_count": 0,
    "event_completed_count": 0,
    "event_retry_count": 0,
    "journal_loss_count": 0,
    "network_stack_free_bytes": 4000,
    "safety_stack_free_bytes": 2000,
    "safety_step_max_us": 100000,
    "safety_interval_max_us": 1100000
  }
}
```

Đây là health fixture có đủ21 keys theo serializer, không phải live capture; các payload khác ở mục8/JSON examples. `values` chứa flat scalar, không nested sensor objects. `ts` là integer **Unix milliseconds tại capture**, không seconds, không uptime. Body ở thời điểm chưa sync capture bỏ cả `ts` và `values` envelope, gửi thẳng flat object và time_synced=false; timestamp Cloud là server receive time. Không thay ts bằng uptime, không backdate offline record chưa sync thành Unix time giả.

| Wire member | JSON type | Ý nghĩa/condition |
|---|---|---|
| ts | integer | Chỉ có khi record.time_synced=true và epoch_ms>0; ms epoch capture |
| values | object | Chỉ có cùng ts; chứa flat scalar telemetry |

`time_synced` là trạng thái lúc **capture record**, không phải lúc broker nhận. Vì vậy một event capture trước sync vẫn gửi dạng server-time sau khi MQTT đã kết nối với clock hợp lệ; không có nghịch lý với TLS gate. Health tạo timestamp mới lúc health capture. State capture lấy last_eval_time_us và wall time bù monotonic delta, không tự freshen capture lúc replay.

| record_kind | Data namespace | Có risk state? | Nghĩa |
|---|---|---|---|
| BOOT | Current/health keys không áp dụng history prefix | Có | First current resync gửi được trong boot; không phải bảo đảm đang ở giai đoạn BOOTING UI |
| RECONNECT | Current | Có | Full latest state sau reconnect |
| SNAPSHOT | Current | Có | Periodic hoặc state-change resync |
| TRANSITION | Historical `event_*` + schema_version/record_kind | Có, tại capture | State-change record/backlog; luôn history dù event_replayed=false |
| HEARTBEAT | Metadata + health counters | Không | Health60s hoặc idle heartbeat30s; không cập nhật risk/latch |

Identity của history: authenticated device originator + event_boot_id + event_record_seq + stream TRANSITION. Current ordering dùng boot_id/record_seq trong current stream; snapshot/event có thể cùng capture ID. Random boot_id không có thứ tự thời gian. Sequence được cấp cả cho captures không publish nên gaps không chứng minh mất message. Same-boot current packet có thể dùng lại một record_seq nếu latest capture chưa đổi; không giả định mỗi accepted MQTT enqueue có ID mới.

**Cảnh báo:** sáu keys current-health `event_drop_count`, `event_expiry_count`, `event_reject_count`, `event_eviction_count`, `event_completed_count`, `event_retry_count` cũng bắt đầu `event_`. Chỉ `record_kind=TRANSITION` mới chọn history namespace. `record_kind`/metadata trong ThingsBoard Latest có thể được cập nhật bởi stream khác; không dùng latest record_kind hoặc HEARTBEAT record_seq làm ordering marker của risk.

<a id="s4"></a>
## 4. Publish cadence

| Stream | IMPLEMENTED condition/policy |
|---|---|
| Acquisition |1000ms; không phải MQTT publish cadence |
| Current measurements |10s khi risk_level=NORMAL và monitoring_status=OK |
| Abnormal current |2s khi risk_level khác NORMAL **hoặc** monitoring_status khác OK; bao gồm UNKNOWN/warmup, SUSPICIOUS, fault |
| State change |Immediate enqueue + wake worker; latest current resync ưu tiên trước history, không chờ10s |
| Health |Due60s; record_kind=HEARTBEAT |
| Idle heartbeat |Due30s kể từ last traffic; dùng cùng health-frame builder, không có riêng kind HEALTH |
| Attributes |Boot/reconnect |

Các thời gian là **due policies, best effort**, không real-time guarantee: phải online, có latest snapshot và in-flight slot; attributes/resync rồi journal/history được scheduler ưu tiên trước periodic/health. Broker outage/backlog có thể làm trễ cadence. Khi NORMAL có traffic10s đều, không kỳ vọng thêm heartbeat mỗi30s; vẫn có health due60s. Bản tin state-change có thể xảy ra giữa hai periodic samples; không suy sensor cadence từ cloud time-series.

<a id="s5"></a>
## 5. Telemetry dictionary đầy đủ

Toàn bộ **84 keys khác nhau trên telemetry topic** =50 current/health +34 historical keys. Một current state record có tối đa36 keys (7 metadata+29 state/measurement); một health record có21 (7+14); union chung7 metadata. Một transition có34 historical keys +2 root routing keys. Optional measurements không luôn xuất hiện. Không tính envelope ts/values vào84.

### 5.1 Metadata chung và current state/measurements

| MQTT Key | JSON Type | Unit | Meaning | Source | Validity | Publish Condition | Example |
|---|---|---|---|---|---|---|---|
| schema_version | integer | — | Phiên bản contract, luôn 1 | hằng số serializer | Bắt buộc trên mọi record | Mọi telemetry record | 1 |
| record_kind | string | — | Phân luồng current, history và health; không phải risk level | publisher chọn BOOT/RECONNECT/SNAPSHOT/TRANSITION; health dùng HEARTBEAT | Bắt buộc; route bằng giá trị này, không chỉ nhìn prefix | Mọi telemetry record | "SNAPSHOT" |
| boot_id | string | — | 128-bit random, 32 ký tự hex theo mỗi boot; không phải device ID ThingsBoard | esp_fill_random tại tb_mqtt_init | Không order bằng từ điển; cần originator + session | Mọi telemetry record | "0123456789abcdef0123456789abcdef" |
| record_seq | integer | — | Counter chung capture/health trong một boot, có khoảng trống; snapshot và transition có thể cùng ID | ++sequence trong capture hoặc health | Chỉ so sequence cùng boot và cùng stream; không phải count sự kiện | Mọi telemetry record | 10 |
| replayed | boolean | — | Cờ retry/provenance của record; current/health hiện tại false | cloud_record_t.replayed | TRANSITION phải archive kể cả cờ false; cờ không phải bộ phân luồng | Mọi telemetry record | false |
| time_synced | boolean | — | Capture có trusted SNTP anchor theo policy firmware | tb_time_is_synced tại capture | True đi cùng envelope ts; false dùng server receive time | Mọi telemetry record | true |
| captured_uptime_ms | integer | ms | Monotonic thời điểm evaluation/capture từ esp_timer; không phải Unix time | risk_state.last_eval_time_us/1000; health dùng thời điểm health | So tuổi trong cùng boot; không lấy heartbeat uptime thay capture time của risk | Mọi telemetry record | 10000 |
| risk_level | string | — | Phân loại nguy cơ cuối cùng, bao gồm ưu tiên latch hiện có | risk_state.risk_level -> risk_level_to_str | Nguồn chuẩn Risk Engine; không suy lại từ measurements | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "NORMAL" |
| risk_reason | string | — | Một primary reason do engine chọn; không có secondary reasons | risk_state.risk_reason -> risk_reason_to_str | Reason có thể mô tả latch/recovery; không đủ để suy current detection | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "NORMAL_SAFE" |
| monitoring_status | string | — | Khả năng giám sát độc lập với mức nguy cơ | risk_state.monitoring_status | FAULT có thể đồng thời EMERGENCY; không ghi đè emergency | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "OK" |
| overall_quality | string | — | Chất lượng tổng hợp pipeline; độc lập risk và per-sensor quality | kitchen_sensor_data_t.v1_quality | Chụp tại pipeline; serializer không tính lại khi send bị chậm; xem per-sensor quality | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "OK" |
| emergency_latched | boolean | — | Chốt emergency cục bộ được WP01 lưu/restore | risk_state.emergency_latched | Không clear qua power-cycle/Cloud ACK; false không tự chứng minh mọi sensor khỏe | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| ack_eligible | boolean | — | Engine cho phép yêu cầu physical ACK theo điều kiện hiện tại | risk_state.ack_eligible | Không phải ACK đã nhận; không phải remote command | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| physical_ack_seq | integer | — | Sequence physical ACK đã được engine chấp nhận clear bền thành công | risk_state.physical_ack_seq từ WP01 | Giữ qua reboot; chỉ delta với baseline đã biết mới chứng minh ACK mới | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | 0 |
| persistence_health | integer | — | Tình trạng lưu latch WP01: 0 PENDING, 1 DURABLE, 2 FAULT | risk_state.persistence_health / latch_store_health_t | Không phải tình trạng event journal; không đảo nghĩa 0 và 2 | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | 1 |
| detection_state | string | — | Bằng chứng emergency hiện tại, tách khỏi alarm đã chốt | risk_state.detection_state | NO_EMERGENCY không đồng nghĩa NORMAL hoặc ack_eligible; UNKNOWN không phải safe | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "NO_EMERGENCY" |
| buzzer_mode | string | — | Mode do driver đang được yêu cầu | buzzer_get_mode() sau local alarm handoff | Không là acoustic feedback; không dùng để phân loại risk | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "OFF" |
| buzzer_output_on | boolean | — | Phase bật/tắt được lấy mẫu; khác commanded mode | kitchen_sensor_data_t.buzzer_state != 0 | False có thể là phase OFF/fixture; không chứng minh đã hết alarm | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| muted | boolean | — | Trạng thái mute cục bộ hiện có, áp dụng WARNING | kitchen_sensor_data_t.is_muted | Không có remote mute; không hiểu như mute emergency | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| mute_remaining_s | integer | s | Đếm ngược mute cục bộ | kitchen_sensor_data_t.mute_remaining_sec | Snapshot countdown; không tự trừ để phát lệnh mute/clear | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | 0 |
| recovery_remaining_s | integer | s | Đếm ngược recovery do engine export | risk_state.recovery_countdown_sec | Không countdown clear latch tự động; đọc state mới làm chuẩn | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | 0 |
| gy906_quality | string | — | Chất lượng acquisition MLX90614; VALID không chứng minh còn nối vật lý | gy906_quality; serializer fresh() với timeout 3000ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture; nhiệt không finite -> INVALID | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "VALID" |
| mq2_quality | string | — | Chất lượng acquisition MQ-2 DO; VALID không chứng minh còn nối vật lý | mq2_quality; serializer fresh() với timeout 3000ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "VALID" |
| flame_quality | string | — | Chất lượng acquisition Flame DO; VALID không chứng minh còn nối vật lý | flame_quality; serializer fresh() với timeout 2500ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "VALID" |
| radar_quality | string | — | Chất lượng acquisition LD2410/UART hoặc OUT; VALID không chứng minh còn nối vật lý | radar_quality; serializer fresh() với timeout 3000ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "VALID" |
| temp_object_c | number | °C | Nhiệt độ vật thể IR đã lọc, không phải nhiệt độ phòng | temp_object_filtered, median-3 hiện có | Chỉ GY906 VALID, gy906_valid và cả hai nhiệt độ finite; quá 3000ms omit | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | 25.4 |
| temp_ambient_c | number | °C | Nhiệt độ ambient của MLX90614 đã lọc, không thay nghĩa thành object | temp_ambient_filtered, median-3 hiện có | Cùng điều kiện temp_object_c; không phải cảm biến phòng độc lập | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | 30.0 |
| temp_rate_cps | number | °C/s | Tốc độ biến thiên object filtered, derived; không là nhiệt độ | dT_dt = delta temp_object_filtered / delta thời gian | Thêm dT_dt_valid và finite; không có mẫu field này trong fixture đã lưu | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | omitted trong fixtures; không bịa giá trị đo |
| gas_detected | boolean | — | DO MQ-2 được pipeline diễn giải thành phát hiện gas/khói | mq2_gas_detected | MQ2 quality VALID và không warmup; không ppm, không raw logic GPIO | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| flame_detected | boolean | — | DO Flame diễn giải FIRE hay SAFE | flame_state == FLAME_STATE_FIRE | Flame quality VALID; không là phép đo cường độ ánh sáng | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| presence | boolean | — | Boolean presence hiện có từ radar/pipeline | radar_presence != 0 | Radar quality VALID; không phải probability/confidence | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | true |
| radar_source | string | — | Nguồn flag radar ở capture: UART hoặc OUT_FALLBACK | radar_uart_valid ? UART : OUT_FALLBACK | Luôn emit trong state record; riêng source không chứng minh dữ liệu valid/fresh | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | "UART" |
| mq2_ao_valid | boolean | — | Flag actual AO acquisition, độc lập MQ2 DO quality | mq2_ao_valid AND own last-update freshness | Age theo mq2_last_update 0..3000ms; không yêu cầu DO VALID/qua warmup | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| mq2_ao_mv_valid | boolean | — | Actual AO có calibrated mV | mq2_ao_mv_valid AND mq2_ao_valid AND freshness | Độc lập DO; flag calibration validity, không phải ppm calibration | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | false |
| mq2_adc_counts | integer | ADC counts | ADC thật tại chân MCU, integer 0..4095; không pseudo-raw từ DO | mq2_ao_adc_raw | mq2_ao_valid + freshness + range 0..4095; omit khi không đủ | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | omitted trong fixtures; không bịa giá trị đo |
| mq2_adc_mv | integer | mV tại chân ADC | Điện áp ADC pin đã calibration, không tự nhân thành AO sensor-side voltage | mq2_ao_adc_mv | Có mq2_adc_counts, mq2_ao_mv_valid và giá trị >=0 | SNAPSHOT/BOOT/RECONNECT: periodic 10s NORMAL/2s abnormal; resync/on-change | omitted trong fixtures; không bịa giá trị đo |

### 5.2 Health — chỉ record_kind=HEARTBEAT

| MQTT Key | JSON Type | Unit | Meaning | Source | Validity | Publish Condition | Example |
|---|---|---|---|---|---|---|---|
| free_heap_bytes | integer | byte | Tổng free heap tại health capture | esp_get_free_heap_size() | Health sample; per-boot low-water nếu có; không suy leak-free từ một sample | HEARTBEAT: health due60s hoặc idle30s; best effort | 100000 |
| min_free_heap_bytes | integer | byte | Boot heap low-water, không phải giá trị hiện tại | esp_get_minimum_free_heap_size() | Health sample; per-boot low-water nếu có; không suy leak-free từ một sample | HEARTBEAT: health due60s hoặc idle30s; best effort | 85000 |
| largest_free_block_bytes | integer | byte | Largest allocatable block 8-bit, chỉ báo fragmentation/headroom | heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) | Health sample; per-boot low-water nếu có; không suy leak-free từ một sample | HEARTBEAT: health due60s hoặc idle30s; best effort | 80000 |
| event_drop_count | integer | lần | Aggregate exposure: rejected + evicted + journal_loss; có overlap, không phải unique incidents mất | s.rejected + s.evicted + journal_loss | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| event_expiry_count | integer | lần | Số lần retry(..., expired=true) trên RAM record; deleted + recycle có thể đếm nhiều lần; không TTL-drop | event_buffer_stats.expired | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| event_reject_count | integer | lần | Số lần push bị từ chối do full buffer; retry cùng trạng thái có thể được đếm nhiều lần | event_buffer_stats.rejected | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| event_eviction_count | integer | lần | Số lower-priority unclaimed RAM records bị thay | event_buffer_stats.evicted | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| event_completed_count | integer | lần | Số RAM records removed bằng ID sau matching PUBACK; journal-only restore không nhất thiết tăng | event_buffer_stats.completed | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| event_retry_count | integer | lần | Số lần event_buffer_retry tìm thấy RAM record, gồm defer khi journal cùng ID đang in-flight; không phải distinct wire retransmissions | event_buffer_stats.retries | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| journal_loss_count | integer | lần | Failed/unavailable/full/corrupt durable copies và uncertain operations; không exact Cloud-loss count | critical_journal_losses() | Counter RAM theo boot, reset khi reboot; dùng boot_id khi tính delta | HEARTBEAT: health due60s hoặc idle30s; best effort | 0 |
| network_stack_free_bytes | integer | byte | Minimum unused stack network owner; không phải MQTT task stack | uxTaskGetStackHighWaterMark(network task) | Health sample; per-boot low-water nếu có; không suy leak-free từ một sample | HEARTBEAT: health due60s hoặc idle30s; best effort | 4000 |
| safety_stack_free_bytes | integer | byte | Minimum unused stack main/safety được copy qua API | uxTaskGetStackHighWaterMark(main task) | Health sample; per-boot low-water nếu có; không suy leak-free từ một sample | HEARTBEAT: health due60s hoặc idle30s; best effort | 2000 |
| safety_step_max_us | integer | µs | Observed max từ đầu iteration tới sau Local Alarm Controller | tb_safety_timing: max step_us | Per boot; không phải physical stimulus-to-audible response hoặc worst-case guarantee | HEARTBEAT: health due60s hoặc idle30s; best effort | 100000 |
| safety_interval_max_us | integer | µs | Observed max khoảng giữa hai safety iteration starts | tb_safety_timing: max interval_us | Per boot; không phải deadline guarantee | HEARTBEAT: health due60s hoặc idle30s; best effort | 1100000 |

### 5.3 Toàn bộ34 historical keys — chỉ record_kind=TRANSITION

Root schema_version và record_kind vẫn không prefix. Các field dưới là tại capture; quality/freshness đánh giá ở capture, không tại replay. Không có history-health payload.

| MQTT Key | JSON Type | Unit | Meaning | Source | Validity | Publish Condition | Example |
|---|---|---|---|---|---|---|---|
| event_boot_id | string | — | Lịch sử tại capture: 128-bit random, 32 ký tự hex theo mỗi boot; không phải device ID ThingsBoard | esp_fill_random tại tb_mqtt_init | Không order bằng từ điển; cần originator + session; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "0123456789abcdef0123456789abcdef" |
| event_record_seq | integer | — | Lịch sử tại capture: Counter chung capture/health trong một boot, có khoảng trống; snapshot và transition có thể cùng ID | ++sequence trong capture hoặc health | Chỉ so sequence cùng boot và cùng stream; không phải count sự kiện; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 10 |
| event_replayed | boolean | — | Lịch sử tại capture: Cờ retry/provenance của record; current/health hiện tại false | cloud_record_t.replayed | TRANSITION phải archive kể cả cờ false; cờ không phải bộ phân luồng; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_time_synced | boolean | — | Lịch sử tại capture: Capture có trusted SNTP anchor theo policy firmware | tb_time_is_synced tại capture | True đi cùng envelope ts; false dùng server receive time; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | true |
| event_captured_uptime_ms | integer | ms | Lịch sử tại capture: Monotonic thời điểm evaluation/capture từ esp_timer; không phải Unix time | risk_state.last_eval_time_us/1000; health dùng thời điểm health | So tuổi trong cùng boot; không lấy heartbeat uptime thay capture time của risk; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 10000 |
| event_risk_level | string | — | Lịch sử tại capture: Phân loại nguy cơ cuối cùng, bao gồm ưu tiên latch hiện có | risk_state.risk_level -> risk_level_to_str | Nguồn chuẩn Risk Engine; không suy lại từ measurements; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "WARNING" |
| event_risk_reason | string | — | Lịch sử tại capture: Một primary reason do engine chọn; không có secondary reasons | risk_state.risk_reason -> risk_reason_to_str | Reason có thể mô tả latch/recovery; không đủ để suy current detection; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "GAS_SMOKE_ELEVATED" |
| event_monitoring_status | string | — | Lịch sử tại capture: Khả năng giám sát độc lập với mức nguy cơ | risk_state.monitoring_status | FAULT có thể đồng thời EMERGENCY; không ghi đè emergency; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "OK" |
| event_overall_quality | string | — | Lịch sử tại capture: Chất lượng tổng hợp pipeline; độc lập risk và per-sensor quality | kitchen_sensor_data_t.v1_quality | Chụp tại pipeline; serializer không tính lại khi send bị chậm; xem per-sensor quality; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "OK" |
| event_emergency_latched | boolean | — | Lịch sử tại capture: Chốt emergency cục bộ được WP01 lưu/restore | risk_state.emergency_latched | Không clear qua power-cycle/Cloud ACK; false không tự chứng minh mọi sensor khỏe; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_ack_eligible | boolean | — | Lịch sử tại capture: Engine cho phép yêu cầu physical ACK theo điều kiện hiện tại | risk_state.ack_eligible | Không phải ACK đã nhận; không phải remote command; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_physical_ack_seq | integer | — | Lịch sử tại capture: Sequence physical ACK đã được engine chấp nhận clear bền thành công | risk_state.physical_ack_seq từ WP01 | Giữ qua reboot; chỉ delta với baseline đã biết mới chứng minh ACK mới; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 0 |
| event_persistence_health | integer | — | Lịch sử tại capture: Tình trạng lưu latch WP01: 0 PENDING, 1 DURABLE, 2 FAULT | risk_state.persistence_health / latch_store_health_t | Không phải tình trạng event journal; không đảo nghĩa 0 và 2; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 1 |
| event_detection_state | string | — | Lịch sử tại capture: Bằng chứng emergency hiện tại, tách khỏi alarm đã chốt | risk_state.detection_state | NO_EMERGENCY không đồng nghĩa NORMAL hoặc ack_eligible; UNKNOWN không phải safe; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "NO_EMERGENCY" |
| event_buzzer_mode | string | — | Lịch sử tại capture: Mode do driver đang được yêu cầu | buzzer_get_mode() sau local alarm handoff | Không là acoustic feedback; không dùng để phân loại risk; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "INTERMITTENT" |
| event_buzzer_output_on | boolean | — | Lịch sử tại capture: Phase bật/tắt được lấy mẫu; khác commanded mode | kitchen_sensor_data_t.buzzer_state != 0 | False có thể là phase OFF/fixture; không chứng minh đã hết alarm; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_muted | boolean | — | Lịch sử tại capture: Trạng thái mute cục bộ hiện có, áp dụng WARNING | kitchen_sensor_data_t.is_muted | Không có remote mute; không hiểu như mute emergency; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_mute_remaining_s | integer | s | Lịch sử tại capture: Đếm ngược mute cục bộ | kitchen_sensor_data_t.mute_remaining_sec | Snapshot countdown; không tự trừ để phát lệnh mute/clear; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 0 |
| event_recovery_remaining_s | integer | s | Lịch sử tại capture: Đếm ngược recovery do engine export | risk_state.recovery_countdown_sec | Không countdown clear latch tự động; đọc state mới làm chuẩn; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 0 |
| event_gy906_quality | string | — | Lịch sử tại capture: Chất lượng acquisition MLX90614; VALID không chứng minh còn nối vật lý | gy906_quality; serializer fresh() với timeout 3000ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture; nhiệt không finite -> INVALID; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "VALID" |
| event_mq2_quality | string | — | Lịch sử tại capture: Chất lượng acquisition MQ-2 DO; VALID không chứng minh còn nối vật lý | mq2_quality; serializer fresh() với timeout 3000ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "VALID" |
| event_flame_quality | string | — | Lịch sử tại capture: Chất lượng acquisition Flame DO; VALID không chứng minh còn nối vật lý | flame_quality; serializer fresh() với timeout 2500ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "VALID" |
| event_radar_quality | string | — | Lịch sử tại capture: Chất lượng acquisition LD2410/UART hoặc OUT; VALID không chứng minh còn nối vật lý | radar_quality; serializer fresh() với timeout 3000ms | VALID/INVALID/STALE/INITIALIZING; current kiểm age lúc encode; history kiểm tại capture; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "VALID" |
| event_temp_object_c | number | °C | Lịch sử tại capture: Nhiệt độ vật thể IR đã lọc, không phải nhiệt độ phòng | temp_object_filtered, median-3 hiện có | Chỉ GY906 VALID, gy906_valid và cả hai nhiệt độ finite; quá 3000ms omit; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 25.4 |
| event_temp_ambient_c | number | °C | Lịch sử tại capture: Nhiệt độ ambient của MLX90614 đã lọc, không thay nghĩa thành object | temp_ambient_filtered, median-3 hiện có | Cùng điều kiện temp_object_c; không phải cảm biến phòng độc lập; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | 30.0 |
| event_temp_rate_cps | number | °C/s | Lịch sử tại capture: Tốc độ biến thiên object filtered, derived; không là nhiệt độ | dT_dt = delta temp_object_filtered / delta thời gian | Thêm dT_dt_valid và finite; không có mẫu field này trong fixture đã lưu; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | omitted trong fixtures; không bịa giá trị đo |
| event_gas_detected | boolean | — | Lịch sử tại capture: DO MQ-2 được pipeline diễn giải thành phát hiện gas/khói | mq2_gas_detected | MQ2 quality VALID và không warmup; không ppm, không raw logic GPIO; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | true |
| event_flame_detected | boolean | — | Lịch sử tại capture: DO Flame diễn giải FIRE hay SAFE | flame_state == FLAME_STATE_FIRE | Flame quality VALID; không là phép đo cường độ ánh sáng; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_presence | boolean | — | Lịch sử tại capture: Boolean presence hiện có từ radar/pipeline | radar_presence != 0 | Radar quality VALID; không phải probability/confidence; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | true |
| event_radar_source | string | — | Lịch sử tại capture: Nguồn flag radar ở capture: UART hoặc OUT_FALLBACK | radar_uart_valid ? UART : OUT_FALLBACK | Luôn emit trong state record; riêng source không chứng minh dữ liệu valid/fresh; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | "UART" |
| event_mq2_ao_valid | boolean | — | Lịch sử tại capture: Flag actual AO acquisition, độc lập MQ2 DO quality | mq2_ao_valid AND own last-update freshness | Age theo mq2_last_update 0..3000ms; không yêu cầu DO VALID/qua warmup; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_mq2_ao_mv_valid | boolean | — | Lịch sử tại capture: Actual AO có calibrated mV | mq2_ao_mv_valid AND mq2_ao_valid AND freshness | Độc lập DO; flag calibration validity, không phải ppm calibration; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | false |
| event_mq2_adc_counts | integer | ADC counts | Lịch sử tại capture: ADC thật tại chân MCU, integer 0..4095; không pseudo-raw từ DO | mq2_ao_adc_raw | mq2_ao_valid + freshness + range 0..4095; omit khi không đủ; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | omitted trong fixtures; không bịa giá trị đo |
| event_mq2_adc_mv | integer | mV tại chân ADC | Lịch sử tại capture: Điện áp ADC pin đã calibration, không tự nhân thành AO sensor-side voltage | mq2_ao_adc_mv | Có mq2_adc_counts, mq2_ao_mv_valid và giá trị >=0; không dùng làm current state dù event_replayed=false | TRANSITION: đầu tiên hoặc thay đổi tuple trạng thái; retry/offline replay | omitted trong fixtures; không bịa giá trị đo |

### 5.4 Validity và missing data

- GY906: quality VALID + gy906_valid + cả object/ambient finite mới gửi nhiệt; dT/dt cần dT_dt_valid và finite. Object/ambient là median-filtered hiện có, dT/dt từ object filtered. Không đổi vai trò hai nhiệt độ.
- DO gas/flame/presence là booleans được diễn giải; MQ2 warmup không emit gas_detected. Raw `mq2_do` và `flame_do` không gửi. VALID chỉ chứng minh acquisition/validation phần mềm tại capture, không chứng minh dây/cảm biến còn nối tốt.
- Freshness timeout ở serializer: GY9063000ms, MQ23000ms, flame2500ms, radar3000ms. Quality VALID nhưng timestamp<=0, future timestamp hoặc age vượt timeout trở thành STALE. History dùng capture_us làm now. Current dùng network now nên giá trị quá cũ bị omit; overall_quality/risk vẫn là snapshot engine, không được serializer classify lại.
- MQ2 AO có validity riêng; có thể emit dù DO quality chưa VALID/warmup. ao_fresh dùng mq2_last_update và age0..3000ms; range raw0..4095, mV>=0 khi calibrated. Không suy ppm; ADC counts/mV tại MCU pin không phải sensor concentration hoặc điện áp trước divider.
- Không gửi NaN/Infinity/null để thay invalid measurement; firmware **omit key**. False là giá trị boolean hợp lệ, không missing. Omission không tự clear old Latest value trong ThingsBoard; widget phải dùng quality/validity + capture/session freshness và clear/placeholder khi không valid. Không forward-fill old temperature thành sample mới.
- captured_uptime_ms là monotonic capture time, không field uptime-seconds riêng. Counters và health maxima theo boot; reset không phải delta âm/loss đã được xóa khỏi lịch sử. event_retry_count đếm cả claim-defer khi journal cùng ID đang in-flight; event_expiry_count có thể đếm nhiều lần cho một deleted/recycle path, không quy đổi1:1 thành packet retry.


<a id="s6"></a>
## 6. Enum dictionary

| Field(s) | Values đúng chữ hoa/thường | Ý nghĩa |
|---|---|---|
| record_kind | `SNAPSHOT`, `BOOT`, `RECONNECT`, `HEARTBEAT`, `TRANSITION` | Phân luồng current, history và health; không phải risk level |
| risk_level | `UNKNOWN`, `NORMAL`, `SUSPICIOUS`, `WARNING`, `EMERGENCY` | Phân loại nguy cơ cuối cùng, bao gồm ưu tiên latch hiện có |
| risk_reason | `NONE`, `INITIALIZING`, `FIRE_DETECTED`, `CRITICAL_TEMPERATURE`, `HIGH_TEMPERATURE`, `RAPID_TEMPERATURE_RISE`, `UNATTENDED_COOKING`, `GAS_SMOKE_ELEVATED`, `SENSOR_FAULT`, `SENSOR_STALE`, `RECOVERY_PENDING`, `EMERGENCY_LATCHED`, `EMERGENCY_ACKNOWLEDGED`, `NORMAL_SAFE` | Một primary reason do engine chọn; không có secondary reasons |
| monitoring_status | `OK`, `DEGRADED`, `FAULT` | Khả năng giám sát độc lập với mức nguy cơ |
| detection_state | `EMERGENCY_PRESENT`, `NO_EMERGENCY`, `UNKNOWN` | Bằng chứng emergency hiện tại, tách khỏi alarm đã chốt |
| overall_quality | `OK`, `DEGRADED`, `CRITICAL` | Chất lượng tổng hợp pipeline; độc lập risk và per-sensor quality |
| gy906_quality / mq2_quality / flame_quality / radar_quality | `INITIALIZING`, `VALID`, `INVALID`, `STALE` | Các sensor độc lập; VALID không là chứng nhận physical health |
| buzzer_mode | `OFF`, `CONTINUOUS`, `INTERMITTENT`, `FAULT_BEEP` | Mode do driver đang được yêu cầu |
| radar_source | `UART`, `OUT_FALLBACK` | Nguồn flag radar ở capture: UART hoặc OUT_FALLBACK |
| persistence_health | integer0=PENDING, 1=DURABLE, 2=FAULT | Latch persistence WP01, không event journal |
| reset_reason (attribute) | POWERON, SOFTWARE, BROWNOUT, OTHER | SDK reset cause được rút gọn |

EMERGENCY_LATCHED và SENSOR_FAULT là **risk_reason**, không risk_level mới. Không thêm NORMAL/WARNING/FIRE enum màu RGB vào model Cloud. `detection_state=NO_EMERGENCY` không loại trừ WARNING và không tự cấp physical ACK.

| risk_reason | Nghĩa trực tiếp theo engine |
|---|---|
| NONE | Không reason cụ thể |
| INITIALIZING | Chưa đủ điều kiện khởi tạo/warmup |
| FIRE_DETECTED | Engine nhận Flame FIRE hợp lệ |
| CRITICAL_TEMPERATURE | Engine nhận object temperature tới mức emergency |
| HIGH_TEMPERATURE | Engine nhận high-temperature warning |
| RAPID_TEMPERATURE_RISE | Engine nhận rapid rise theo luật hiện có, có phối hợp gas |
| UNATTENDED_COOKING | Engine nhận unattended cooking |
| GAS_SMOKE_ELEVATED | Engine nhận gas/smoke warning từ DO, không calibrated ppm |
| SENSOR_FAULT | Engine ghi sensor fault |
| SENSOR_STALE | Engine ghi stale sensor data |
| RECOVERY_PENDING | Engine đang chờ khoảng safe liên tục |
| EMERGENCY_LATCHED | Emergency alarm vẫn chốt, không tự clear |
| EMERGENCY_ACKNOWLEDGED | Engine đã chấp nhận physical clear; reason transient, không phải Cloud ACK |
| NORMAL_SAFE | Engine đánh giá trạng thái NORMAL an toàn theo logic hiện có |

Fallback phòng thủ của string functions nếu internal enum ngoài miền: risk_level/monitoring_status -> UNDEFINED, risk_reason -> UNKNOWN_REASON; strict schema hiện không liệt kê các giá trị lỗi này. Consumer không đổi chúng thành NORMAL: đưa vào unsupported/diagnostic path. Quality không nhận diện map INITIALIZING, detection không nhận diện map UNKNOWN, buzzer mode ngoài miền map OFF, overall quality ngoài OK/DEGRADED map CRITICAL. Đây là defensive serialization, không state nghiệp vụ mới để tạo classifier Cloud.


<a id="s7"></a>
## 7. Client attributes

| MQTT Key | JSON Type | Unit | Meaning | Source | Validity | Publish Condition | Example |
|---|---|---|---|---|---|---|---|
| reset_reason | string | — | Reset cause được rút gọn POWERON/SOFTWARE/BROWNOUT/OTHER | esp_reset_reason() | Không thể suy nguyên nhân reset vật lý chi tiết từ OTHER | Boot/reconnect, QoS1; không gửi mỗi sample | "POWERON" |
| schema_version | integer | — | Contract version 1 | hằng số 1 | Client attribute, khác topic telemetry | Boot/reconnect, QoS1; không gửi mỗi sample | 1 |
| firmware_version | string | — | Release label hiện tại 1.1.0-phase1 | PCCC_FIRMWARE_VERSION do build generator | Fixture dùng nhãn giả fixture-1.1.0, không dùng làm deployed version | Boot/reconnect, QoS1; không gửi mỗi sample | "fixture-1.1.0" |
| build_id | string | — | 16 hex fingerprint public sources/build files/config/provisioned flag | PCCC_BUILD_ID | Không phải app SHA256 hoặc device identity; fixture-build là placeholder | Boot/reconnect, QoS1; không gửi mỗi sample | "fixture-build" |
| config_id | string | — | 16 hex fingerprint safety_config + board_pins + sdkconfig | PCCC_CONFIG_ID | Không hash/expose credential values; fixture-config là placeholder | Boot/reconnect, QoS1; không gửi mỗi sample | "fixture-config" |
| hardware_model | string | — | Nhãn phần cứng do firmware khai báo ESP32-S3 N16R8 | hằng string serializer | Không là autodetection board/revision | Boot/reconnect, QoS1; không gửi mỗi sample | "ESP32-S3 N16R8" |
| zone_id | string | — | Nhãn zone hiện tại V1 | hằng string serializer | Client-claimed label, không thay authenticated originator | Boot/reconnect, QoS1; không gửi mỗi sample | "V1" |
| capabilities | string | — | Danh sách comma-separated trong một JSON string | hằng string serializer | Không JSON array; capabilities không chứng minh từng sensor nối tốt | Boot/reconnect, QoS1; không gửi mỗi sample | "mlx90614,mq2_do,mq2_ao,flame_do,flame_ao,ld2410,physical_ack,buzzer,rgb,oled,read_only_cloud" |
| recommended_inactivity_ms | integer | ms | Advertise giá trị khuyến nghị 90000 | hằng số serializer | Không tự cài đặt server inactivity hoặc tạo offline alarm | Boot/reconnect, QoS1; không gửi mỗi sample | 90000 |

Cả9 attributes là scalar và được gửi trên attributes topic, không trong telemetry stream. capabilities là một **string CSV** với giá trị:

`mlx90614,mq2_do,mq2_ao,flame_do,flame_ao,ld2410,physical_ack,buzzer,rgb,oled,read_only_cloud`

flame_ao là capability phần cứng/diagnostic, không có flame AO telemetry trong contract này. recommended_inactivity_ms=90000 là lời advertise; chưa set server inactivity. firmware_version trong fixture là fixture-1.1.0 còn bản firmware thực label1.1.0-phase1; build/config fixture placeholders không phải deployed identities. Không có shared attributes nhận vào firmware, không Cloud threshold/mute/latch control.


<a id="s8"></a>
## 8. Payload examples

File `PCCC_V1_PAYLOAD_EXAMPLES.json` có một JSON wrapper chứa examples array. **Chỉ entry.payload là wire body**; wrapper/id/topic/origin/derivation/limitations không được publish như telemetry fields. Mười case verbatim từ fixtures; năm current projections chỉ đổi prefix/kind theo đúng cloud_encode; một replay projection chỉ đổi event_replayed. Không invent số đo, không sửa fixtures nguồn và không chạy serializer/build trong tác vụ documentation này.

Các cases dùng chung boot_id/seq10/ts trong fixtures và phải test **độc lập**, không là sequence NORMAL→WARNING→EMERGENCY. Không gửi cả bộ vào production. Reset/isolate test-originator ordering/dedup state giữa cases; test replay dùng chính same ID để xác minh dedup. Các projections chỉ TESTED về JSON/schema, không là HIL/Risk Engine trace. SENSOR_FAULT/BOOT fixtures có defaults thiếu nhất quán với pipeline, xem mục14.

### NORMAL — fixture verbatim

Provenance: `fixtures/pccc-v1/normal.json`; Verbatim JSON values từ fixture; chỉ pretty-print

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "SNAPSHOT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "risk_level": "NORMAL",
    "risk_reason": "NORMAL_SAFE",
    "monitoring_status": "OK",
    "overall_quality": "OK",
    "emergency_latched": false,
    "ack_eligible": false,
    "physical_ack_seq": 0,
    "persistence_health": 1,
    "detection_state": "NO_EMERGENCY",
    "buzzer_mode": "OFF",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "temp_object_c": 25.4,
    "temp_ambient_c": 30.0,
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "radar_source": "UART",
    "mq2_ao_valid": false,
    "mq2_ao_mv_valid": false
  }
}
```

### WARNING — current projection từ warning_gas

Provenance: `fixtures/pccc-v1/warning_gas.json`; Projection documented: strip event_ on historical keys, set record_kind=SNAPSHOT; giữ nguyên mọi value/ID/ts. Đúng hai nhánh prefix/kind của cloud_encode, không chạy firmware để tạo mẫu này.

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "SNAPSHOT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "risk_level": "WARNING",
    "risk_reason": "GAS_SMOKE_ELEVATED",
    "monitoring_status": "OK",
    "overall_quality": "OK",
    "emergency_latched": false,
    "ack_eligible": false,
    "physical_ack_seq": 0,
    "persistence_health": 1,
    "detection_state": "NO_EMERGENCY",
    "buzzer_mode": "INTERMITTENT",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "temp_object_c": 25.4,
    "temp_ambient_c": 30.0,
    "gas_detected": true,
    "flame_detected": false,
    "presence": true,
    "radar_source": "UART",
    "mq2_ao_valid": false,
    "mq2_ao_mv_valid": false
  }
}
```

### EMERGENCY — current projection từ emergency

Provenance: `fixtures/pccc-v1/emergency.json`; Projection documented: strip event_ on historical keys, set record_kind=SNAPSHOT; giữ nguyên mọi value/ID/ts. Đúng hai nhánh prefix/kind của cloud_encode, không chạy firmware để tạo mẫu này.

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "SNAPSHOT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "risk_level": "EMERGENCY",
    "risk_reason": "FIRE_DETECTED",
    "monitoring_status": "OK",
    "overall_quality": "OK",
    "emergency_latched": true,
    "ack_eligible": false,
    "physical_ack_seq": 0,
    "persistence_health": 1,
    "detection_state": "EMERGENCY_PRESENT",
    "buzzer_mode": "CONTINUOUS",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "temp_object_c": 25.4,
    "temp_ambient_c": 30.0,
    "gas_detected": false,
    "flame_detected": true,
    "presence": true,
    "radar_source": "UART",
    "mq2_ao_valid": false,
    "mq2_ao_mv_valid": false
  }
}
```

### EMERGENCY_LATCHED — current projection, không thêm risk_level enum

Provenance: `fixtures/pccc-v1/emergency_latched.json`; Projection documented: strip event_ on historical keys, set record_kind=SNAPSHOT; giữ nguyên mọi value/ID/ts. Đúng hai nhánh prefix/kind của cloud_encode, không chạy firmware để tạo mẫu này.

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "SNAPSHOT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "risk_level": "EMERGENCY",
    "risk_reason": "EMERGENCY_LATCHED",
    "monitoring_status": "OK",
    "overall_quality": "OK",
    "emergency_latched": true,
    "ack_eligible": true,
    "physical_ack_seq": 0,
    "persistence_health": 1,
    "detection_state": "NO_EMERGENCY",
    "buzzer_mode": "CONTINUOUS",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "temp_object_c": 25.4,
    "temp_ambient_c": 30.0,
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "radar_source": "UART",
    "mq2_ao_valid": false,
    "mq2_ao_mv_valid": false
  }
}
```

### SENSOR_FAULT — current projection, giữ nguyên fixture overall_quality=OK; không dùng field đó phủ nhận fault

Provenance: `fixtures/pccc-v1/sensor_fault.json`; Projection documented: strip event_ on historical keys, set record_kind=SNAPSHOT; giữ nguyên mọi value/ID/ts. Đúng hai nhánh prefix/kind của cloud_encode, không chạy firmware để tạo mẫu này.

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "SNAPSHOT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "risk_level": "UNKNOWN",
    "risk_reason": "SENSOR_FAULT",
    "monitoring_status": "FAULT",
    "overall_quality": "OK",
    "emergency_latched": false,
    "ack_eligible": false,
    "physical_ack_seq": 0,
    "persistence_health": 1,
    "detection_state": "UNKNOWN",
    "buzzer_mode": "FAULT_BEEP",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "INVALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "radar_source": "UART",
    "mq2_ao_valid": false,
    "mq2_ao_mv_valid": false
  }
}
```

### Physical ACK / recovery — post-ACK NORMAL projection, seq1 không chứng minh ACK mới nếu không có baseline

Provenance: `fixtures/pccc-v1/recovery_normal.json`; Projection documented: strip event_ on historical keys, set record_kind=SNAPSHOT; giữ nguyên mọi value/ID/ts. Đúng hai nhánh prefix/kind của cloud_encode, không chạy firmware để tạo mẫu này.

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "SNAPSHOT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "risk_level": "NORMAL",
    "risk_reason": "NORMAL_SAFE",
    "monitoring_status": "OK",
    "overall_quality": "OK",
    "emergency_latched": false,
    "ack_eligible": false,
    "physical_ack_seq": 1,
    "persistence_health": 1,
    "detection_state": "NO_EMERGENCY",
    "buzzer_mode": "OFF",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "temp_object_c": 25.4,
    "temp_ambient_c": 30.0,
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "radar_source": "UART",
    "mq2_ao_valid": false,
    "mq2_ao_mv_valid": false
  }
}
```

### BOOT — unsynced capture, fixture minh họa chứ không engine boot trace

Provenance: `fixtures/pccc-v1/boot_unsynced.json`; Verbatim JSON values từ fixture; chỉ pretty-print

```json
{
  "schema_version": 1,
  "record_kind": "BOOT",
  "boot_id": "0123456789abcdef0123456789abcdef",
  "record_seq": 10,
  "replayed": false,
  "time_synced": false,
  "captured_uptime_ms": 10000,
  "risk_level": "UNKNOWN",
  "risk_reason": "INITIALIZING",
  "monitoring_status": "OK",
  "overall_quality": "OK",
  "emergency_latched": false,
  "ack_eligible": false,
  "physical_ack_seq": 0,
  "persistence_health": 1,
  "detection_state": "NO_EMERGENCY",
  "buzzer_mode": "OFF",
  "buzzer_output_on": false,
  "muted": false,
  "mute_remaining_s": 0,
  "recovery_remaining_s": 0,
  "gy906_quality": "INITIALIZING",
  "mq2_quality": "INITIALIZING",
  "flame_quality": "INITIALIZING",
  "radar_quality": "INITIALIZING",
  "radar_source": "UART",
  "mq2_ao_valid": false,
  "mq2_ao_mv_valid": false
}
```

### RECONNECT — unsynced capture format, không phải TLS bypass

Provenance: `fixtures/pccc-v1/reconnect_unsynced.json`; Verbatim JSON values từ fixture; chỉ pretty-print

```json
{
  "schema_version": 1,
  "record_kind": "RECONNECT",
  "boot_id": "0123456789abcdef0123456789abcdef",
  "record_seq": 10,
  "replayed": false,
  "time_synced": false,
  "captured_uptime_ms": 10000,
  "risk_level": "NORMAL",
  "risk_reason": "NORMAL_SAFE",
  "monitoring_status": "OK",
  "overall_quality": "OK",
  "emergency_latched": false,
  "ack_eligible": false,
  "physical_ack_seq": 0,
  "persistence_health": 1,
  "detection_state": "NO_EMERGENCY",
  "buzzer_mode": "OFF",
  "buzzer_output_on": false,
  "muted": false,
  "mute_remaining_s": 0,
  "recovery_remaining_s": 0,
  "gy906_quality": "VALID",
  "mq2_quality": "VALID",
  "flame_quality": "VALID",
  "radar_quality": "VALID",
  "temp_object_c": 25.4,
  "temp_ambient_c": 30.0,
  "gas_detected": false,
  "flame_detected": false,
  "presence": true,
  "radar_source": "UART",
  "mq2_ao_valid": false,
  "mq2_ao_mv_valid": false
}
```

### Offline critical replay — documented flag projection, archive-only

Provenance: `fixtures/pccc-v1/emergency.json`; Chỉ set event_replayed=true đúng journal-service path tb_mqtt.c; identity, timestamp, measurements giữ nguyên

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "TRANSITION",
    "event_boot_id": "0123456789abcdef0123456789abcdef",
    "event_record_seq": 10,
    "event_replayed": true,
    "event_time_synced": true,
    "event_captured_uptime_ms": 10000,
    "event_risk_level": "EMERGENCY",
    "event_risk_reason": "FIRE_DETECTED",
    "event_monitoring_status": "OK",
    "event_overall_quality": "OK",
    "event_emergency_latched": true,
    "event_ack_eligible": false,
    "event_physical_ack_seq": 0,
    "event_persistence_health": 1,
    "event_detection_state": "EMERGENCY_PRESENT",
    "event_buzzer_mode": "CONTINUOUS",
    "event_buzzer_output_on": false,
    "event_muted": false,
    "event_mute_remaining_s": 0,
    "event_recovery_remaining_s": 0,
    "event_gy906_quality": "VALID",
    "event_mq2_quality": "VALID",
    "event_flame_quality": "VALID",
    "event_radar_quality": "VALID",
    "event_temp_object_c": 25.4,
    "event_temp_ambient_c": 30.0,
    "event_gas_detected": false,
    "event_flame_detected": true,
    "event_presence": true,
    "event_radar_source": "UART",
    "event_mq2_ao_valid": false,
    "event_mq2_ao_mv_valid": false
  }
}
```

### HEARTBEAT — health fixture

Provenance: `fixtures/pccc-v1/health.json`; Verbatim JSON values từ fixture; chỉ pretty-print

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "record_kind": "HEARTBEAT",
    "boot_id": "0123456789abcdef0123456789abcdef",
    "record_seq": 10,
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 10000,
    "free_heap_bytes": 100000,
    "min_free_heap_bytes": 85000,
    "largest_free_block_bytes": 80000,
    "event_drop_count": 0,
    "event_expiry_count": 0,
    "event_reject_count": 0,
    "event_eviction_count": 0,
    "event_completed_count": 0,
    "event_retry_count": 0,
    "journal_loss_count": 0,
    "network_stack_free_bytes": 4000,
    "safety_stack_free_bytes": 2000,
    "safety_step_max_us": 100000,
    "safety_interval_max_us": 1100000
  }
}
```

### Client attributes — fixture placeholders, không deployed identities

Provenance: `fixtures/pccc-v1/attributes.json`; Verbatim JSON values từ fixture; chỉ pretty-print

```json
{
  "reset_reason": "POWERON",
  "schema_version": 1,
  "firmware_version": "fixture-1.1.0",
  "build_id": "fixture-build",
  "config_id": "fixture-config",
  "hardware_model": "ESP32-S3 N16R8",
  "zone_id": "V1",
  "capabilities": "mlx90614,mq2_do,mq2_ao,flame_do,flame_ao,ld2410,physical_ack,buzzer,rgb,oled,read_only_cloud",
  "recommended_inactivity_ms": 90000
}
```


<a id="s9"></a>
## 9. Event lifecycle và offline

Transition tạo ở first accepted state handoff và khi bất kỳ field sau đổi so với last queued state: risk_level, risk_reason, monitoring_status, emergency_latched, physical_ack_seq, ack_eligible, persistence_health, is_muted, detection_state, gy906_quality, mq2_quality, flame_quality, radar_quality. Temperature/countdown/buzzer phase thay đổi đơn thuần không tạo transition. Snapshot queue vẫn nhận latest kể cả event push bị reject; previous chỉ advance khi queue nhận thành công.

RAM priority: emergency_latched hoặc risk EMERGENCY -> EMERGENCY; tiếp theo risk WARNING -> WARNING; monitoring FAULT -> FAULT; còn lại INFO. Accepted physical ACK sau clear latch thường INFO/RAM-only; ACK sequence vẫn durable trong WP01. Không suy persistent event priority từ buzzer phase.

Lifecycle IMPLEMENTED:

1. Capture immutable identity/state -> push vào static RAM32 slots.
2. Consumer atomically claim copy; không giữ lock trong lúc MQTT.
3. MQTT enqueue accepted trả MID; event **chưa completed** và vẫn giữ record.
4. Ledger tối đa4 correlate client epoch+MID -> event ID. Matching PUBACK mới complete/remove đúng ID, rồi clear đúng journal key. Control/current/health cũng chia sẻ budget4; không tất cả record đều là event.
5. Duplicate/out-of-order/stale-epoch ACK không xóa event khác. MID reuse trong client lifetime bị quarantine bằng bitmap và recycle client; retry dùng same event identity. Các metadata transport replay flag có thể đổi, core capture ID/state không đổi.
6. Disconnect/error/ACK-attempt timeout>35s/deleted message/callback overflow/clock mất trust -> release/retry claims, destroy outbox cũ và reconnect bounded. Retry có thể gây duplicate ngay cả khi broker đã nhận trước disconnect.

Queue full chỉ evict unclaimed lower-priority record; không evict claimed hoặc critical cùng priority để lấy record mới. Full-critical reject có counter; không block safety loop vô hạn. History priority order không phải chronological order; seq trong cùng boot giúp phân tích, không có global ordering across boots/journal slots. Current resync ưu tiên trước backlog nhưng không bảo đảm đã được DB commit trước history.

Critical journal: tối đa4 unacknowledged critical copies NVS `pccc_events/ev0..ev3`, version+CRC+readback; chỉ EMERGENCY-priority transitions, không every sample, không full incident database. Có thể survive reboot nếu commit xong. Full/unavailable/corrupt/read/write errors tăng loss/exposure counter; corrupt/unreadable slot quarantine. Journal copy được encode với event_replayed=true ngay lúc persist; **cờ true không chứng minh record đã từng được broker nhận hoặc trải qua disconnect**. Reconnect/retry cũng set replayed=true cho RAM events. Không có offline message TTL để tự xóa critical records.

Giới hạn IMPLEMENTED: RAM-only history mất qua reboot; chỉ4 durable slots; mất nguồn trước commit có thể mất event; broker PUBACK xóa journal mặc dù Rule Chain/DB có thể chưa xử lý. Loss/retry counters là per-boot RAM, có overlap và không phải exact incident count; quá khứ trước reboot có thể UNKNOWN. `event_completed_count` chỉ tăng khi remove RAM record; ACK journal-only restore có thể không tăng. Các internal created/queued/accepted/duplicates, queue depth, outbox bytes, in-flight, transport epoch/MID **không có telemetry keys riêng** (một số chỉ log). **QoS1 không là exactly-once. PUBACK không chứng minh Rule Chain/database commit.**

<a id="s10"></a>
## 10. Hướng dẫn Cloud routing — RECOMMENDED — NOT IMPLEMENTED

Đây là hướng dẫn để người phụ trách ThingsBoard tự cấu hình; không có Rule Chain production đã được tạo/đọc trong tác vụ này. Dry-run Python reference hiện có không phải deployable Rule Chain. Không cần mở ESP32 source để làm theo thứ tự:

1. Xác định authenticated device originator từ ThingsBoard ingress. Phân biệt attributes topic với telemetry; attributes cập nhật client metadata, không trigger risk alarm.
2. Tại ingress xác định ThingsBoard có đã normalize native ts/values thành message/metadata chưa. Nếu còn wire envelope thì lấy ts/values; nếu đã normalize không unwrap lần hai. Validate schema_version=1, known record_kind và scalar types; quarantine legacy/unknown schema/enums, không map thành NORMAL.
3. Route **trước khi save current state hoặc trigger alarms**:
   - TRANSITION: chỉ lưu archive/engineering history; dedup durable theo originator+event_boot_id+event_record_seq+TRANSITION. Dùng event_time_synced để phân biệt epoch capture/server time. Không copy event_risk_level sang risk_level, không update current latch hoặc clear/create live alarm từ delayed history.
   - HEARTBEAT: health-only. Lưu health metrics riêng; không advance current risk high-water, không ghi đè current capture metadata chỉ vì health seq lớn hơn.
   - BOOT/RECONNECT/SNAPSHOT: current-state path. Serialize processing per device. Chỉ order record_seq trong cùng boot; cùng seq duplicate/older không rollback. Unknown/new boot cần verified session boundary từ authenticated ingress/session handling; không lexicographically order random boot IDs hoặc chấp nhận bất kỳ BOOT muộn như new session. Nếu không xác minh được session, quarantine và ghi NOT VERIFIED thay vì đoán.
4. Giữ riêng raw telemetry/archive và accepted current materialization. Device keys không có namespace mới do Cloud tự tạo trong tài liệu này; nếu triển khai derived server fields, đánh dấu chúng server-derived, không nhầm firmware keys.
5. Event/current có thể cùng capture ID và ts; routing phải xảy ra trước dedup để không làm mất snapshot vì history cùng ID. Archive bảo toàn nhiều sự kiện cùng timestamp bằng composite identity, không chỉ timestamp làm primary key.
6. Current widgets lấy **accepted current record**, health widgets lấy health record. Vì HEALTH shared metadata keys với current, ThingsBoard Latest từng key có thể không tạo thành một coherent snapshot; không ghép risk cũ với boot/seq/uptime của heartbeat mới. Sensor missing/quality invalid -> placeholder và retain history có quality, không nhân bản old sample.
7. Alarm evaluation chỉ đọc accepted current firmware state, không recreate sensor thresholds trên server. Existing legacy Rule Chain/dashboard đang xử lý ra sao **NOT VERIFIED**, phải inspect trước mapping. Cloud setup và RBAC cần tài khoản tenant phù hợp, device publish credential không cấp quyền này.


<a id="s11"></a>
## 11. ThingsBoard Alarm mapping — RECOMMENDED — NOT IMPLEMENTED

Mọi hành vi Cloud trong cột cuối là **RECOMMENDED — NOT IMPLEMENTED**, không tuyên bố có sẵn. Live emergency incident dựa trên current risk_level=EMERGENCY hoặc emergency_latched=true; detection_state chỉ giúp phân biệt nguy hiểm còn hiện diện/chưa biết/đã hạ, không là điều kiện bổ sung để bỏ qua emergency. Firmware condition ở ba cột đầu là dữ liệu IMPLEMENTED. Điều kiện dưới chỉ chọn/hiển thị state đã có, không tính lại Risk Engine.

| Điều kiện firmware | Trường MQTT liên quan (accepted current) | Ý nghĩa | Hành vi Cloud đề xuất — NOT IMPLEMENTED |
|---|---|---|---|
| NORMAL | risk_level=NORMAL; vẫn đọc monitoring_status/quality/latch | Engine NORMAL, không phải chứng nhận tất cả sensor nối tốt | Hiển thị giám sát; recovery/clear warning chỉ từ accepted current; không clear emergency chỉ vì một measurement bình thường |
| SUSPICIOUS | risk_level=SUSPICIOUS, risk_reason | Nghi ngờ theo engine | Hiển thị đang theo dõi và primary reason, không tự nâng EMERGENCY |
| WARNING | risk_level=WARNING; reason có thể GAS_SMOKE_ELEVATED/HIGH_TEMPERATURE/RAPID_TEMPERATURE_RISE | Warning có nhiều nguyên nhân | Một warning incident/device/type; update reason thay vì tạo duplicate mỗi snapshot; khi escalates emergency giữ history |
| EMERGENCY còn hiện diện | risk_level=EMERGENCY, detection_state=EMERGENCY_PRESENT, emergency_latched | Nguy hiểm hiện tại cùng alarm chốt | Ưu tiên emergency; update cùng active incident, không clear vì network/sensor lỗi thứ cấp |
| EMERGENCY_LATCHED | emergency_latched=true; thường risk_level=EMERGENCY/risk_reason=EMERGENCY_LATCHED; detection_state có thể NO_EMERGENCY hoặc UNKNOWN | Chốt còn hiệu lực dù hazard evidence đã hạ hoặc chưa đủ dữ liệu | Giữ cùng emergency incident, hiển thị latched; dùng ack_eligible để hướng dẫn physical ACK khi true; false/UNKNOWN không báo safe hoặc tự clear |
| SENSOR_FAULT / SENSOR_STALE | monitoring_status=FAULT và per-sensor quality; reason SENSOR_FAULT/SENSOR_STALE khi engine chọn | Fault độc lập risk; emergency vẫn có thể giữ ưu tiên reason | Fault indicator/alarm riêng, không overwrite/clear emergency. Chỉ rõ sensor bằng quality, không khẳng định đã đứt dây từ GPIO |
| Physical ACK accepted / recovery | physical_ack_seq tăng; emergency_latched=false trong current authoritative record; có thể thấy reason EMERGENCY_ACKNOWLEDGED transient rồi NORMAL_SAFE | Physical latch đã được engine clear bền; một packet riêng với seq>0 không chứng minh ACK vừa mới xảy ra | Ghi physical-ACK history khi baseline xác minh được; clear emergency theo accepted current state/sequence/session và policy, không giả Cloud ACK đã xảy ra; không đòi bắt được transient reason mới nhận recovery |
| Device offline | Không có firmware offline key/LWT; recommended_inactivity_ms attribute=90000 | Chưa nhận traffic, không chứng minh thiết bị đã tắt hoặc hết cháy | Cấu hình server-side inactivity90s rồi test tenant; alarm offline riêng; không clear emergency khi offline hoặc khi Cloud ACK |

Risk Engine state, physical latch, Cloud Alarm active/cleared và Cloud Alarm acknowledged/unacknowledged là bốn trục khác nhau. UI Cloud ACK chỉ tác động Cloud record; không gọi thiết bị, không tăng physical_ack_seq, không tắt còi. Khi current latch đã false nhưng Cloud alarm còn unacknowledged, giữ lịch sử và trạng thái acknowledgment của Cloud; không delete incident để giả đồng bộ. Device reconnect NORMAL với lịch sử FIRE chỉ archive lịch sử, không mở lại emergency hiện tại. Nếu mới thấy detection UNKNOWN thì hiển thị UNKNOWN, không tạo kết luận an toàn từ reason string.


<a id="s12"></a>
## 12. Dashboard field mapping — RECOMMENDED — NOT IMPLEMENTED

Đây chỉ là cách dùng dữ liệu, không tạo Dashboard trong tác vụ bàn giao.

### Operational View

| Khu vực | Field/data source | Quy tắc hiển thị đề xuất |
|---|---|---|
| Current risk/reason | Accepted current risk_level, risk_reason | State text rõ; SUSPICIOUS/WARNING/EMERGENCY theo enum thật |
| Temperature | temp_object_c + gy906_quality/capture freshness | Object là số chính, ambient ghi nhãn riêng nếu cần; invalid/stale/missing -> — |
| Latch/physical ACK | emergency_latched, detection_state, ack_eligible, physical_ack_seq | Latch nổi bật; chỉ hướng dẫn giữ nút vật lý khi ack_eligible; không remote buttons |
| Monitoring/health | monitoring_status và bốn sensor qualities | Mô tả degraded/fault; không chứng nhận physical connection |
| Online/last seen | Server connectivity/inactivity và accepted current capture context | Khác với last history arrival; recent replay không chứng minh recent sensor sample |
| Active alarm | Server Alarm model sau khi Rule Chain được triển khai | Không có firmware field active_alarm; độc lập physical latch/Cloud ACK |
| Alarm history | Server incidents + event_* archive/dedup | Event là lịch sử tại capture; phân biệt captured time đáng tin và received time |

### Engineering View

| Khu vực | Firmware fields | Cách dùng |
|---|---|---|
| Sensor quality | gy906_quality, mq2_quality, flame_quality, radar_quality, overall_quality | Per-sensor luôn ưu tiên validity/age; aggregate là snapshot, không freshness tổng mới |
| Temperature/rate | temp_object_c, temp_ambient_c, temp_rate_cps | °C / °C/s; gaps khi invalid, không interpolate thành mẫu thật |
| MQ2 DO | gas_detected, mq2_quality | Boolean detection, không ppm; raw GPIO không publish |
| MQ2 AO | mq2_ao_valid, mq2_ao_mv_valid, mq2_adc_counts, mq2_adc_mv | Counts0..4095, mV ADC pin; tách validity/calibration khỏi DO |
| Flame | flame_detected, flame_quality | Boolean DO interpretation; flame AO không publish |
| Radar | presence, radar_quality, radar_source | UART/OUT_FALLBACK; không có distances/energies/target mode keys trong contract hiện tại |
| Local output | buzzer_mode, buzzer_output_on, muted, mute_remaining_s | Command vs phase; không acoustic feedback |
| Recovery/latch | recovery_remaining_s, detection_state, emergency_latched, ack_eligible, physical_ack_seq, persistence_health | Không tự run countdown để clear; persistence_health chỉ latch |
| Uptime/session | captured_uptime_ms, boot_id, record_seq | ms monotonic; dùng current và health contexts riêng |
| Resources | free_heap_bytes, min_free_heap_bytes, largest_free_block_bytes, network_stack_free_bytes, safety_stack_free_bytes, safety_step_max_us, safety_interval_max_us | Per-boot trends; không chứng minh2s physical response từ step_max |
| Delivery exposure | event_drop_count, event_expiry_count, event_reject_count, event_eviction_count, event_completed_count, event_retry_count, journal_loss_count | Counters per boot, có overlap; không dùng aggregate như exact lost incidents |
| Firmware/schema/config | Client attrs firmware_version, build_id, config_id, hardware_model, zone_id, capabilities; schema_version trên records | Fixture placeholders không phải deployed version |
| Timestamp/replay | record_kind, time_synced, ts nếu có; event_time_synced, event_replayed, event_boot_id, event_record_seq, event_captured_uptime_ms | Kiểm trusted capture vs receive time, cross-boot historical order; không route chỉ bằng prefix |

Không kỳ vọng node_id, timestamp, seq, T_object, MQ2_raw, mq2_do, flame_raw, flame_state, radar_distance/energy, uptime, nested status.* hoặc telemetry.* từ serializer cũ. NODE_ID macro không được publish. Không có secondary_reasons, online bool, event_id, event_priority hoặc Cloud alarm status trong wire contract hiện tại.


<a id="s13"></a>
## 13. Integration & acceptance checklist

Xem file đồng hành [cloud-integration-checklist.md](cloud-integration-checklist.md). Đây là các bước cho thành viên ThingsBoard thực hiện trên môi trường/tenant được phép; trạng thái live tests **NOT VERIFIED**, không được hiểu là tác vụ này đã chạy chúng. Checklist không yêu cầu đọc source, không cần thay firmware hoặc tạo lửa/khói thật.


<a id="s14"></a>
## 14. Known Discrepancies và giới hạn tích hợp

| ID | Sai lệch/giới hạn đã đối chiếu | Cách hiểu đúng, không sửa source |
|---|---|---|
| KD01 | Schema history cho phép thêm14 keys health có event_ prefix, nhưng cloud_encode TRANSITION không emit health | Count history thực34, không48; không tạo dashboard đợi các field này |
| KD02 | Phase1 data dictionary history_key sinh cơ học có cả event_schema_version và event-health names không được emit; record_kind không có history_key | Routing keys schema_version/record_kind giữ root; bảng mục5 là authoritative emitted keys |
| KD03 | Attributes fixture dùng firmware_version=fixture-1.1.0, build_id=fixture-build, config_id=fixture-config | Placeholder của host tests; deployed label1.1.0-phase1 và last-verified fingerprints đầu tài liệu; không giả đây là payload capture |
| KD04 | sensor_fault fixture và projection giữ overall_quality=OK dù GY906 INVALID, monitoring FAULT | Serializer chép nguyên input v1_quality; pipeline hiện có sẽ DEGRADED khi một sensor invalid. Đây là fixture defaults thiếu nhất quán end-to-end; vẫn valid wire schema; không dùng aggregate OK phủ nhận fault |
| KD05 | boot_unsynced fixture có mọi quality INITIALIZING nhưng aggregate OK/monitoring OK/detection NO_EMERGENCY | Chỉ serialization fixture, không chứng minh một actual boot state hợp lệ của Risk Engine/pipeline; không derive alarm policy từ defaults đó |
| KD06 | Fixtures dùng cùng boot_id, record_seq10 và ts; sample quality OK không chứng minh sensor được nối/test vật lý | Independent examples, không chronological trace; reset test state giữa cases hoặc dùng real captured independent records khi acceptance |
| KD07 | recovery_normal fixture reason NORMAL_SAFE với physical_ack_seq1 | Post-ACK illustration, không packet ngay lúc ACK và không bằng chứng nút vừa được giữ nếu không baseline |
| KD08 | Existing fixtures không có temp_rate_cps/mq2_adc_counts/mq2_adc_mv samples | Branch/type/unit xác minh từ serializer và schema, không bịa measurements. Optional branches được TESTED ở host Phase1 theo report, không tạo thêm HIL evidence ở tác vụ này |
| KD09 | Historical records luôn history dù event_replayed=false; journal encode set true ngay trước first send có thể chưa từng delivered | Route bằng record_kind=TRANSITION, không dùng replayed false làm current hoặc true làm proof of redelivery |
| KD10 | Không có offline telemetry/LWT; inactivity chỉ advertised90000, server config/tenant/Cloud alarms chưa inspect | Cần member có quyền tenant thực hiện checklist; không claim device token có quyền admin |
| KD11 | QoS1/PUBACK và4 durable copies không là exactly-once/DB durability; counters per boot, finite persistence/precommit windows | Dedup/order/archive riêng, chấp nhận history gaps có policy; không tự clear emergency từ data absence |
| KD12 | capture metadata được dùng chung trên health/current; schema producer strict nhưng chưa enforce mọi semantic coupling | Materialize per-stream coherent current, validate quality/types/age, không ghép Latest keys tùy ý. Không coi schema PASS là engine/E2E/HIL PASS |

14 schema-only history keys (NOT EMITTED):

`event_event_completed_count`, `event_event_drop_count`, `event_event_eviction_count`, `event_event_expiry_count`, `event_event_reject_count`, `event_event_retry_count`, `event_free_heap_bytes`, `event_journal_loss_count`, `event_largest_free_block_bytes`, `event_min_free_heap_bytes`, `event_network_stack_free_bytes`, `event_safety_interval_max_us`, `event_safety_stack_free_bytes`, `event_safety_step_max_us`

`event_schema_version` không được serializer emit; `event_record_kind` cũng không có. Root health counters có prefix event_ là counters, không lịch sử. Scope bàn giao không sửa schema/fixtures/data dictionary Phase1, không build/flash/fault injection và không thay Cloud.

Giới hạn kiểm chứng: actual tenant ingest/DB/Rule Chain/alarm/dashboard chưa VERIFIED; standard SNTP không authenticated; wrong-host/expired/revoked cert HIL, power-cut journal và long soak chưa có evidence Phase1. Không thêm yêu cầu reliability/refactor ở tài liệu bàn giao này; người tích hợp chỉ cần không hiểu nhầm phạm vi bảo đảm dữ liệu hiện có.


<a id="s15"></a>
## 15. Source references và validation

Mọi semantics để người nhận sử dụng đã có trong tài liệu/bảng CSV; references để traceability, không bắt buộc người triển khai mở source. Source hiện tại ưu tiên hơn proposal/review cũ.

- [main/src/cloud_codec.c:31](../../main/src/cloud_codec.c#L31)
- [main/src/cloud_codec.c:42](../../main/src/cloud_codec.c#L42)
- [main/src/cloud_codec.c:74](../../main/src/cloud_codec.c#L74)
- [main/src/cloud_codec.c:82](../../main/src/cloud_codec.c#L82)
- [main/src/tb_mqtt.c:23](../../main/src/tb_mqtt.c#L23)
- [main/src/tb_mqtt.c:74](../../main/src/tb_mqtt.c#L74)
- [main/src/tb_mqtt.c:118](../../main/src/tb_mqtt.c#L118)
- [main/include/tb_mqtt.h:3](../../main/include/tb_mqtt.h#L3)
- [main/include/publish_policy.h:5](../../main/include/publish_policy.h#L5)
- [main/include/clock_policy.h:4](../../main/include/clock_policy.h#L4)
- [main/src/event_buffer.c:10](../../main/src/event_buffer.c#L10)
- [main/src/mqtt_delivery.c:10](../../main/src/mqtt_delivery.c#L10)
- [main/src/critical_journal.c:31](../../main/src/critical_journal.c#L31)
- [main/include/sensor_types.h:31](../../main/include/sensor_types.h#L31)
- [main/src/risk_engine.c:55](../../main/src/risk_engine.c#L55)
- [main/src/risk_engine.c:174](../../main/src/risk_engine.c#L174)
- [main/include/latch_store.h:5](../../main/include/latch_store.h#L5)
- [main/src/sensor_pipeline.c:402](../../main/src/sensor_pipeline.c#L402)
- [main/include/sensor_pipeline.h:19](../../main/include/sensor_pipeline.h#L19)
- `managed_components/espressif__mqtt/mqtt_client.c:596` (tham chiếu cục bộ, không phát hành public)
- `managed_components/espressif__mqtt/mqtt_client.c:228` (tham chiếu cục bộ, không phát hành public)
- [sdkconfig:3860](../../sdkconfig#L3860)
- [sdkconfig:3159](../../sdkconfig#L3159)
- [tools/generate_build_config.py:20](../../tools/generate_build_config.py#L20)
- [docs/production/phase1-implementation-report.md:118](../production/phase1-implementation-report.md#L118)
- `docs/production/evidence/phase1/boot_final.log:139` (tham chiếu cục bộ, không phát hành public)
- [schemas/pccc-v1/telemetry.schema.json:2](../../schemas/pccc-v1/telemetry.schema.json#L2)
- [schemas/pccc-v1/data_dictionary.json:2](../../schemas/pccc-v1/data_dictionary.json#L2)
- [schemas/pccc-v1/attributes_dictionary.json:50](../../schemas/pccc-v1/attributes_dictionary.json#L50)
- [docs/reviews/thingsboard-data-model-proposal.md:1](../reviews/thingsboard-data-model-proposal.md#L1)
- [docs/reviews/production-readiness-review.md:1](../reviews/production-readiness-review.md#L1)
- [docs/reviews/production-implementation-plan.md:1](../reviews/production-implementation-plan.md#L1)
- [migration/phase2/normalize.py:4](../../migration/phase2/normalize.py#L4)

Validation documentation: đối chiếu extracted literal keys của begin/cloud_encode/cloud_health/cloud_attributes với CSV và schema; enum đối chiếu source; JSON16 entries parse/validate bằng Draft2020-12, optional validity checks và provenance transformation checks; Markdown JSON blocks/type/unit/link references được kiểm tra; scan credentials không xuất giá trị; so before/after hashes để chỉ docs/thingsboard được thêm. Kết quả cụ thể ở cuối checklist. Không chạy lại host binaries/build/flash hoặc fault injection. Evidence TESTED firmware nêu ở trên lấy từ báo cáo/log Phase1 hiện có, không nhận là mới test trong tác vụ này.

