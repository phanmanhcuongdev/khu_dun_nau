# PCCC V1 — ThingsBoard data model proposal

Ngày: 2026-11-11. **PROPOSAL ONLY**: không sửa serializer, device profile, Rule Chain, dashboard, credential hoặc dữ liệu Cloud. Đồng hành với [audit](production_readiness_review.md). Các source citations là đường dẫn tương đối từ repository.

## 1. Current MQTT audit — hiện tại, không phải schema đề xuất

| Hạng mục | Code thực tế |
|---|---|
| Broker | `mqtts://iot.cuongdso.id.vn:8883`, `main/include/tb_mqtt.h:9`; TLS ISRG Root X1, token username (`tb_mqtt.c:308–318`). Cloud trực tiếp hay proxy/bridge: UNKNOWN |
| Protocol/topic | MQTT3.1.1 (`sdkconfig:3860`); chỉ publish `v1/devices/me/telemetry` (`tb_mqtt.c:29,249,266`), QoS1, retain0 |
| Subscribe/commands | Không subscribe, không RPC, không xử lý MQTT_EVENT_DATA; không cloud command/remote threshold |
| Telemetry cadence | Queue latest1 từ mỗi safety step; publisher consume khi connected. Main ngủ1000ms cộng acquisition/log; log thực~1.04–1.13s, không rate limiter riêng |
| Transition | Risk/reason đổi -> event RAM32; monitoring-only/mute/ACK không trigger (`main.c:145`). FIRE tạo event ngay sau evaluation, không chờ periodic queue, nhưng app event mutex trước còi có thể50ms |
| Reconnect | MQTT library auto reconnect mặc định; app backoff1→60s chỉ sleep, không gọi reconnect; có thể ngủ sau CONNECTED; Wi-Fi reconnect mỗi disconnect |
| Delivery | Publish accepted xóa event trước PUBACK; library outbox RAM default expiry30s; MQTT_EVENT_PUBLISHED chỉ log. Không end-to-end persistence proof |
| Offline | Snapshot latest-only, events bounded priority RAM; không NVS persistence; event sequence reset boot; không drop counters/resync handshake |
| Time | `timestamp` custom seconds; telemetry ở publish-time, event ở detection-time hoặc uptime seconds; time_synced chỉ wallclock>1700000000. Không native `ts`/`values` |
| Attributes | Không publish/client/shared/server attrs từ app; NODE_ID/ZONE_ID lặp trong telemetry |
| Last will | Không cấu hình; ThingsBoard inactivity phía server chưa được kiểm tra |

### Payload shape hiện tại (giá trị minh họa, không đo mới)

```json
{"node_id":"ESP32_S3_MINI_APT_01","zone_id":"V1","timestamp":0,"time_synced":false,"seq":1,"telemetry":{"T_object":25.4,"dT_dt":0.1,"MQ2_raw":200,"presence":1,"flame_state":"SAFE","temp_ambient":30.0,"radar_mode":"Static","radar_distance":0,"mq2_do":1,"flame_raw":4095,"mq2_ao_adc_raw":60,"mq2_ao_adc_mv":54,"mq2_ao_valid":true,"flame_ao_adc_raw":4095,"flame_ao_adc_mv":3153,"flame_ao_valid":true,"radar_uart_valid":true,"radar_target_state":2,"radar_moving_distance_cm":0,"radar_stationary_distance_cm":60,"radar_moving_energy":0,"radar_stationary_energy":80,"buzzer_state":0,"mute_state":0,"uptime":1},"status":{"risk_level":"NORMAL","monitoring_status":"OK","data_quality":"VALID","reason":"NORMAL_SAFE"}}
```

Event hiện tại:

```json
{"node_id":"ESP32_S3_MINI_APT_01","zone_id":"V1","timestamp":15,"time_synced":false,"message_type":"SAFETY_EVENT","event_id":1,"event_priority":1,"event_detail":{"risk_level":"EMERGENCY","monitoring_status":"OK","reason":"FIRE_DETECTED","T_object":25.4,"MQ2_raw":200,"flame_fire":true,"emergency_latched":true}}
```

Timestamp event15 ở đây là uptime, **không phải epoch**. Cả hai payload hợp lệ JSON nhưng không tự biến `telemetry.T_object` thành key scalar `T_object` trên ThingsBoard. Theo API, scalar/object ở gốc là telemetry key/value; server có thể lưu `telemetry`/`status` dưới dạng JSON value, không tự flatten. Không khẳng định tenant hiện lỗi vì chưa inspect Rule Chain; nó có thể đã có transform. `timestamp` custom không phải `ts` native. [ThingsBoard MQTT telemetry](https://thingsboard.io/docs/paas/reference/mqtt-api/telemetry/)

### Toàn bộ telemetry keys đang tạo

Nguồn serializer `tb_mqtt.c:94–188`; source measurements `sensor_pipeline.c:123–305`.

| Keys | Type hiện tại / đơn vị | Semantics/validity thật |
|---|---|---|
| node_id, zone_id | string / none | Định danh cấu hình lặp mỗi message |
| timestamp, time_synced, seq | number(s), bool, number | Timestamp publish seconds hoặc0; sequence chỉ telemetry, reset reboot |
| T_object | number °C hoặc null | Object **filtered**; gy906_valid+finite, không check age ở network |
| temp_ambient | number °C, hoặc omitted | Ambient filtered, gy906_valid+finite |
| dT_dt | number °C/s hoặc null | Derived từ object filtered, có dT_dt_valid+finite |
| MQ2_raw | number / unitless | Moving average của **pseudo-raw200/1000 từ DO**, không phải ADC (`mq2_sensor.c:87–88`) |
| presence | number0/1 | UART state OR OUT, fallback OUT; luôn gửi không quality mask |
| flame_state | string FIRE/SAFE | DO state; có thể safe default khi driver read fail; gửi không quality mask |
| radar_mode, radar_target_state | string / enum0..3 | Trùng thông tin khi UART valid; mode có thể fallback STATIC từ OUT |
| radar_distance | number / cm | Chỉ moving distance, không lựa chọn distance “gần nhất”/stationary; unconditional |
| mq2_do, flame_raw | number / logic, unitless | DO0/1 và pseudo-raw0/4095, flame_raw không phải AO ADC |
| mq2_ao_adc_raw, flame_ao_adc_raw | integer / ADC counts | ADC thật, chỉ thêm nếu AO read valid |
| mq2_ao_adc_mv, flame_ao_adc_mv | integer / mV tại chân ADC | Chỉ thêm nếu calibration valid; không là nồng độ hoặc đơn vị quang học |
| mq2_ao_valid, flame_ao_valid, radar_uart_valid | bool | Transport/read validity, không chứng minh dây/sensor khỏe |
| radar_moving_distance_cm, radar_stationary_distance_cm | integer / cm | Chỉ gửi UART valid; không chứng minh có target ở từng loại nếu mode không tương ứng |
| radar_moving_energy, radar_stationary_energy | integer / relative sensor scale | Không phải joule, không phải confidence probability |
| buzzer_state, mute_state | number0/1 | Output phase và mute flag; không acoustic feedback |
| uptime | number / s | Từ pipeline_init, không từ reset (init sensors trước đó) |
| status.risk_level / monitoring_status / data_quality / reason | string enums | Risk engine state; aggregate quality mapOK→VALID, DEGRADED→DEGRADED, CRITICAL→INVALID |

Event thêm `message_type`, `event_id`, `event_priority`, `event_detail.*`, latch; không có quality snapshot trong event. Temp finite nhưng có thể old/invalid (`main.c:164`, `tb_mqtt.c:206`). Không có per-sensor quality, recovery seconds, continuous latch snapshot, accepted physical ACK identity, firmware/schema/boot identity, outbox/drop/resource counters.

## 2. Logical model và boundary

**Một thiết bị ThingsBoard cho một node V1**, asset/zone chỉ để phân nhóm khi có nhiều node. ThingsBoard device originator/authentication là identity chính; không lặp NODE_ID trong mọi sample. Zone/site là server metadata hoặc client claimed zone có phân biệt quyền sở hữu.

Năm nhóm:

1. Measurements: object/ambient filtered; dT/dt; DO-derived gas detected; flame; presence. ADC thật tùy engineering profile, không ppm.
2. Risk: exact risk_level/reason/monitoring, overall_quality và recovery. Không tự tính gas/temp thresholds trên Cloud.
3. Local alarm: demand/mode, output phase, physical latch, mute, physical ACK đã áp dụng. Cloud ACK hoàn toàn riêng.
4. Health: sensor quality/source, uptime, boot/reset, tối thiểu heap/drop counters để thấy degradation.
5. Metadata: version/schema/model/capabilities/config identity ít thay đổi, dùng attributes.

Không có secondary-reasons thực tế: **không phát `secondary_reasons` giả**. Không biến boolean buzzer phase thành risk. Không suy ra hết nguy hiểm chỉ vì reason LATCHED vì reason có thể linger. Nếu cần tách detection khỏi alarm trên dashboard, chỉ export read-only predicates/eligibility ngay nơi engine đã tính; không tính lại threshold trong Cloud hoặc bộ classifier LED/OLED khác.

## 3. Data dictionary / classification — proposed V1 contract

Quy ước category: **T** time-series snapshot; **E** transition/event (vẫn publish qua native telemetry); **C** client attributes; **S** server attributes; **D** diagnostic-only. Không có shared attributes cần thiết ở V1. “New export” là phải triển khai sau duyệt, không có sẵn trong payload hiện tại.

Cadence **P**: NORMAL measurements10s, WARNING/EMERGENCY2s, thay đổi state gửi ngay; health60s; heartbeat30s khi không message khác; snapshot resync mọi reconnect. Đây là policy đề xuất, không phải timer đo/risk (vẫn cadence local hiện có).

| Field | Type | Unit | Source | Validity | Category | Trigger/Interval | Purpose |
|---|---|---|---|---|---|---|---|
| schema_version | integer=1 | — | New contract constant | Always | T/E + C | Mỗi record + boot | Route đúng serializer |
| boot_id | string128-bit hex | — | New random boot identity | Unique per boot, không token/MAC secret | T/E | Mỗi record | Dedup/cross-reboot ordering |
| record_seq | integer safe JSON <=2^53−1 | — | New global record counter | Tăng trong boot, mọi kind | T/E | Mỗi record | Dedup tuple boot_id+seq |
| record_kind | enum | — | SNAPSHOT/TRANSITION/HEARTBEAT/BOOT/RECONNECT | Always | T/E | Mỗi record | Routing current/history |
| replayed | bool | — | Offline backlog bookkeeping mới | Always | T/E | Mỗi record | Không apply lịch sử vào current alarm |
| time_synced | bool | — | New SNTP status+clock age policy | Không chỉ year check | T/E | Mỗi record | Timestamp trust |
| captured_uptime_ms | integer | ms | New esp_timer acquisition/evaluation instant | Monotonic trong boot | T/E | Mỗi record | Time khi chưa có epoch |
| temp_object_c | number | °C | temp_object_filtered | Gy VALID+fresh+finite | T/E context | P | Nhiệt vật thể cho trend/risk context |
| temp_ambient_c | number | °C | temp_ambient_filtered | Same MLX validity | T | 30s / abnormal snapshot | Context môi trường, không thay object |
| temp_rate_cps | number | °C/s | dT_dt | dT_dt_valid+Gy VALID+fresh+finite | T/E context | P | Hiểu rapid-rise event |
| gas_detected | bool | — | mq2_gas_detected (DO) | MQ2 VALID/not warmup | T/E | P/on change | Đúng đầu vào nghiệp vụ đang có |
| flame_detected | bool | — | flame_state | Flame VALID; physical connectivity unverified | T/E | P/on change | Trạng thái DO, không gọi optical certainty |
| presence | bool | — | radar_presence | Radar VALID + source disclosed | T | P/on change | Context người hiện diện |
| risk_level | enum UNKNOWN/NORMAL/SUSPICIOUS/WARNING/EMERGENCY | — | risk_state.risk_level | Authoritative evaluation | T/E | Immediate change + P | Source of truth |
| risk_reason | 14 string enums hiện tại | — | risk_state.risk_reason | Always với risk | T/E | Immediate change + P | Nguyên nhân chính |
| monitoring_status | enum OK/DEGRADED/FAULT | — | risk_state.monitoring_status | Always | T/E | Change + P | Khả năng giám sát |
| overall_quality | enum OK/DEGRADED/CRITICAL | — | v1_quality | Giữ semantics enum gốc | T | Change + P | Tổng hợp dữ liệu, không đồng nhất monitoring |
| gy906_quality | enum INITIALIZING/VALID/INVALID/STALE | — | gy906_quality | Luôn gửi khi value absent | T/E | Change + P | Mask dữ liệu nhiệt |
| mq2_quality | same quality enum | — | mq2_quality | DO pipeline quality, không AO validity | T/E | Change + P | Warmup/validity gas |
| flame_quality | same quality enum | — | flame_quality | Không có supervision đứt dây | T/E | Change + P | Mask flame |
| radar_quality | same quality enum | — | radar_quality | Fallback có thể vẫn VALID | T | Change + P | Không giấu fallback |
| radar_source | enum UART/OUT_FALLBACK | — | radar_uart_valid | Source current sample | T | Change + P | Không diễn giải OUT như UART data |
| recovery_remaining_s | integer>=0 | s | recovery_countdown_sec | 0=không countdown; không có nghĩa safe | T/E | Change/P | Recovery UI |
| emergency_latched | bool | — | risk_state.emergency_latched | Always trong full state | T/E | Immediate + P | Physical latch authoritative |
| buzzer_mode | enum OFF/CONTINUOUS/INTERMITTENT/FAULT_BEEP | — | local_alarm_status.active_buzzer_mode new export | Commanded demand, không hardware feedback | T/E | Change + P | Phân biệt demand và phase |
| buzzer_output_on | bool | — | buzzer_state sau controller | Commanded pin phase | T | P | Engineering output trace |
| muted | bool | — | is_muted | Never suppress EMERGENCY | T/E | Change + P | Trạng thái mute |
| mute_remaining_s | integer>=0 | s | mute_remaining_sec | 0 khi không mute | T | P khi mute | UI |
| physical_ack_seq | integer>=0 | — | New counter ở branch latch clear thực sự do physical request | Count successful applications, không đếm request bị reject | T/E | On successful clear + resync | Phân biệt Cloud ACK; không dùng transient emergency_acknowledged làm history |
| detection_state | enum EMERGENCY_PRESENT/NO_EMERGENCY/UNKNOWN | — | New read-only export existing fire/critical predicates | PRESENT nếu predicate true; NO_EMERGENCY nếu đủ sensor xác minh false; UNKNOWN khi thiếu dữ liệu | T/E (recommended export) | Change + P | Tách physical emergency input khỏi risk latch; không là risk classifier mới |
| ack_eligible | bool | — | New read-only export existing latch-recovery predicates | False khi missing/unsafe/unlatched | T/E (recommended export) | Change + P | Hướng dẫn local ACK nhất quán; không command |
| free_heap_bytes | integer | bytes | New esp_get_free_heap_size | Runtime sample | T health | 60s | Memory pressure |
| min_free_heap_bytes | integer | bytes | New esp_get_minimum_free_heap_size | Boot lifetime | T health | 60s | Soak trend |
| event_drop_count | integer | records | New buffer rejected/evicted/expired count | Boot lifetime, reset documented | T health | Change/60s | Observable loss |
| safety_event | JSON object (server derived) | — | Rule Chain archive of immutable transition context | Preserve boot/seq/time/quality | E history | Every transition accepted once | Timeline, không pollute current state |
| firmware_version | string | — | New release version/build identity | Không dùng default project "1" làm release hash | C | Boot/reconnect if changed | Reproducibility |
| build_id | string | — | New build hash/manifest ID | Always | C | Boot/change | Trace binary |
| hardware_model | string | — | Config ESP32-S3 N16R8 | Board revision UNKNOWN không tự điền | C | Boot/change | Inventory |
| capabilities | JSON array/string set | — | Existing enabled sensors/RGB/OLED | Từ release manifest | C | Boot/change | Dashboard routing |
| config_id | string | — | New hash/version reviewed safety configuration | Read-only declaration | C | Boot/change | Audit threshold provenance, không remote tuning |
| reset_reason | string IDF enum | — | New esp_reset_reason | Current boot only | C + BOOT record | Boot | Diagnose reboot |
| zone_id | string | — | Existing ZONE_ID claim | Provisioned | C | Boot/change | Claimed logical zone |
| site/installation label | string | — | Operator | Cloud-owned | S | Installation/change | Friendly name, permissions |
| inactivityTimeout | integer | ms | Cloud device state service | Proposed90000, tenant test needed | S | Configuration | Offline threshold for30s heartbeat |
| active/lastActivityTime | bool/integer | —/epoch ms | ThingsBoard device state service | Server-owned, receipt-based | S | Server-managed | Last seen, không firmware self-report offline |
| mq2_adc_counts / mq2_adc_mv | integer | counts/mV | mq2_ao_adc_raw/mv | AO valid; mv riêng calibration valid | T optional engineering | 10s engineering / event diagnostic | Thực đo tương đối, **không ppm** |
| flame_adc_counts / flame_adc_mv | integer | counts/mV | flame_ao_adc_raw/mv | Same AO conditions | D default, optional T engineering | Fault investigation only | Không dùng thay DO risk |
| radar target_mode, moving/stationary distances, energies | enums/numbers | cm / relative scale | UART fields | UART fresh + target mode tương ứng, otherwise omit | D default, optional T engineering | 10s engineering | Debug presence, không V1 safety dashboard chính |
| raw GPIO, pseudo MQ2_raw/flame_raw, parser dumps, packet bytes | integer/bytes | — | Legacy/diagnostic | Semantics tách rõ | D only | Local diagnostics | Không telemetry mặc định |
| largest_heap_block / stack high-water | integer | bytes | New diagnostic probes | Runtime measured | D default | Soak/debug | Không tạo dozens health timeseries khi chưa cần |

Trong minimum schema, detection_state/ack_eligible có thể chưa export: khi đó **omit**, UI hiển thị UNKNOWN, không suy diễn từ latch/reason. Để hoàn thành dashboard tách bốn loại trạng thái theo yêu cầu review, khuyến nghị thêm read-only export nhỏ ở WP01; không thay luật, ngưỡng hay đường physical ACK. Mọi trường “new” ở bảng đều chưa triển khai.

## 4. Schema đề xuất

Native telemetry envelope trên topic `v1/devices/me/telemetry`:

- Có clock tin cậy: `{"ts": <capture epoch milliseconds>, "values": {...flat fields...}}`.
- Chưa sync: flat key/value object, **không có ts**, ThingsBoard dùng receive time; `time_synced=false` + captured_uptime_ms. Không gửi uptime làm epoch. API lưu các key/value và native timestamp; `ts` dùng milliseconds. [Native telemetry formats](https://thingsboard.io/docs/paas/reference/mqtt-api/telemetry/)
- Client attributes: flat object tới `v1/devices/me/attributes`, không phải nested `attributes` trong telemetry. [Client attributes API](https://thingsboard.io/docs/paas/reference/mqtt-api/attributes/)

### JSON Schema cho envelope và state tối thiểu (proposal, không thêm dependency)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "urn:pccc:v1:telemetry",
  "$defs": {
    "quality": {"enum": ["INITIALIZING", "VALID", "INVALID", "STALE"]},
    "values": {
      "type": "object",
      "required": ["schema_version", "boot_id", "record_seq", "record_kind", "replayed", "time_synced", "captured_uptime_ms"],
      "properties": {
        "schema_version": {"const": 1},
        "boot_id": {"type": "string", "pattern": "^[0-9a-f]{32}$"},
        "record_seq": {"type": "integer", "minimum": 0, "maximum": 9007199254740991},
        "record_kind": {"enum": ["SNAPSHOT", "TRANSITION", "HEARTBEAT", "BOOT", "RECONNECT"]},
        "replayed": {"type": "boolean"},
        "time_synced": {"type": "boolean"},
        "captured_uptime_ms": {"type": "integer", "minimum": 0},
        "risk_level": {"enum": ["UNKNOWN", "NORMAL", "SUSPICIOUS", "WARNING", "EMERGENCY"]},
        "risk_reason": {"enum": ["NONE", "INITIALIZING", "FIRE_DETECTED", "CRITICAL_TEMPERATURE", "HIGH_TEMPERATURE", "RAPID_TEMPERATURE_RISE", "UNATTENDED_COOKING", "GAS_SMOKE_ELEVATED", "SENSOR_FAULT", "SENSOR_STALE", "RECOVERY_PENDING", "EMERGENCY_LATCHED", "EMERGENCY_ACKNOWLEDGED", "NORMAL_SAFE"]},
        "monitoring_status": {"enum": ["OK", "DEGRADED", "FAULT"]},
        "emergency_latched": {"type": "boolean"},
        "gy906_quality": {"$ref": "#/$defs/quality"},
        "mq2_quality": {"$ref": "#/$defs/quality"},
        "flame_quality": {"$ref": "#/$defs/quality"},
        "radar_quality": {"$ref": "#/$defs/quality"},
        "temp_object_c": {"type": "number"},
        "temp_ambient_c": {"type": "number"},
        "temp_rate_cps": {"type": "number"},
        "gas_detected": {"type": "boolean"},
        "flame_detected": {"type": "boolean"},
        "presence": {"type": "boolean"},
        "buzzer_mode": {"enum": ["OFF", "CONTINUOUS", "INTERMITTENT", "FAULT_BEEP"]},
        "buzzer_output_on": {"type": "boolean"},
        "muted": {"type": "boolean"},
        "recovery_remaining_s": {"type": "integer", "minimum": 0},
        "mute_remaining_s": {"type": "integer", "minimum": 0},
        "physical_ack_seq": {"type": "integer", "minimum": 0},
        "overall_quality": {"enum": ["OK", "DEGRADED", "CRITICAL"]},
        "radar_source": {"enum": ["UART", "OUT_FALLBACK"]},
        "detection_state": {"enum": ["EMERGENCY_PRESENT", "NO_EMERGENCY", "UNKNOWN"]},
        "ack_eligible": {"type": "boolean"},
        "free_heap_bytes": {"type": "integer", "minimum": 0},
        "min_free_heap_bytes": {"type": "integer", "minimum": 0},
        "event_drop_count": {"type": "integer", "minimum": 0},
        "reset_reason": {"type": "string"},
        "mq2_adc_counts": {"type": "integer", "minimum": 0, "maximum": 4095},
        "mq2_adc_mv": {"type": "integer", "minimum": 0}
      },
      "additionalProperties": true,
      "allOf": [{
        "if": {"properties": {"record_kind": {"enum": ["SNAPSHOT", "TRANSITION", "RECONNECT"]}}},
        "then": {"required": ["risk_level", "risk_reason", "monitoring_status", "emergency_latched", "buzzer_mode", "muted", "physical_ack_seq", "gy906_quality", "mq2_quality", "flame_quality", "radar_quality"]}
      }]
    }
  },
  "oneOf": [
    {"type": "object", "required": ["ts", "values"], "properties": {"ts": {"type": "integer", "minimum": 1}, "values": {"allOf": [{"$ref": "#/$defs/values"}, {"properties": {"time_synced": {"const": true}}}]}}, "additionalProperties": false},
    {"allOf": [{"$ref": "#/$defs/values"}, {"properties": {"time_synced": {"const": false}}, "not": {"required": ["ts"]}}]}
  ]
}
```

Schema là contract khởi điểm có forward-compatible extra keys; validator runtime/WP03 phải còn kiểm **semantic rules**: finite numbers, measurements chỉ khi quality valid/fresh, NO_EMERGENCY không đồng nghĩa NORMAL, latch/ACK eligibility không được suy từ Cloud. Không tự giới hạn phép đo theo fire thresholds; dải kỹ thuật do sensor driver hiện có.

Attributes schema: JSON object, `schema_version` integer1; `firmware_version/build_id/hardware_model/config_id/zone_id` strings; `capabilities` JSON string array. Field hardware board revision không biết thì omit. reset_reason có thể client attribute boot hiện tại nhưng BOOT telemetry cũng giữ lịch sử reboot; không phải metadata bất biến.

## 5. Seven payload scenarios

**Toàn bộ số đo, timestamp, boot_id, seq dưới đây chỉ minh họa**, không phải measurement mới hoặc kết quả test. Examples dùng sensor thật/biến thực, và các new exports đã đánh dấu trong dictionary. Một TRANSITION là full state tối thiểu, archive event và update current theo ordering guard. Physical ACK counter chỉ tăng khi firmware thực sự clear latch.

Các case là **snapshot độc lập**, dùng boot_id khác nhau; không ghép thành một trace bỏ qua latch. Case fault5 không theo sau latch4.

### 1. NORMAL

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "boot_id": "00000000000000000000000000000001",
    "record_seq": 10,
    "record_kind": "SNAPSHOT",
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 60000,
    "risk_level": "NORMAL",
    "risk_reason": "NORMAL_SAFE",
    "monitoring_status": "OK",
    "emergency_latched": false,
    "physical_ack_seq": 0,
    "buzzer_mode": "OFF",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "overall_quality": "OK",
    "radar_source": "UART",
    "temp_object_c": 25.4,
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "detection_state": "NO_EMERGENCY",
    "ack_eligible": false,
    "temp_ambient_c": 30.0,
    "temp_rate_cps": 0.1
  }
}
```

### 2. WARNING — GAS_SMOKE_ELEVATED (DO active đủ hold, sau warmup)

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "boot_id": "00000000000000000000000000000002",
    "record_seq": 10,
    "record_kind": "TRANSITION",
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 60000,
    "risk_level": "WARNING",
    "risk_reason": "GAS_SMOKE_ELEVATED",
    "monitoring_status": "OK",
    "emergency_latched": false,
    "physical_ack_seq": 0,
    "buzzer_mode": "INTERMITTENT",
    "buzzer_output_on": true,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "overall_quality": "OK",
    "radar_source": "UART",
    "temp_object_c": 25.4,
    "gas_detected": true,
    "flame_detected": false,
    "presence": true,
    "detection_state": "NO_EMERGENCY",
    "ack_eligible": false
  }
}
```

### 3. EMERGENCY — ngọn lửa từ DO

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "boot_id": "00000000000000000000000000000003",
    "record_seq": 10,
    "record_kind": "TRANSITION",
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 60000,
    "risk_level": "EMERGENCY",
    "risk_reason": "FIRE_DETECTED",
    "monitoring_status": "OK",
    "emergency_latched": true,
    "physical_ack_seq": 0,
    "buzzer_mode": "CONTINUOUS",
    "buzzer_output_on": true,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "overall_quality": "OK",
    "radar_source": "UART",
    "temp_object_c": 25.4,
    "gas_detected": false,
    "flame_detected": true,
    "presence": true,
    "detection_state": "EMERGENCY_PRESENT",
    "ack_eligible": false
  }
}
```

### 4. EMERGENCY_LATCHED — inputs đã phục hồi, chưa ACK vật lý

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "boot_id": "00000000000000000000000000000004",
    "record_seq": 10,
    "record_kind": "TRANSITION",
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 60000,
    "risk_level": "EMERGENCY",
    "risk_reason": "EMERGENCY_LATCHED",
    "monitoring_status": "OK",
    "emergency_latched": true,
    "physical_ack_seq": 0,
    "buzzer_mode": "CONTINUOUS",
    "buzzer_output_on": true,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "overall_quality": "OK",
    "radar_source": "UART",
    "temp_object_c": 60.0,
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "detection_state": "NO_EMERGENCY",
    "ack_eligible": true
  }
}
```

### 5. SENSOR_FAULT — GY906 lỗi, chưa có latch lịch sử

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "boot_id": "00000000000000000000000000000005",
    "record_seq": 10,
    "record_kind": "TRANSITION",
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 60000,
    "risk_level": "UNKNOWN",
    "risk_reason": "SENSOR_FAULT",
    "monitoring_status": "FAULT",
    "emergency_latched": false,
    "physical_ack_seq": 0,
    "buzzer_mode": "FAULT_BEEP",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "INVALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "overall_quality": "DEGRADED",
    "radar_source": "UART",
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "detection_state": "UNKNOWN",
    "ack_eligible": false
  }
}
```

### 6. Recovery to NORMAL — sau physical ACK đã được áp dụng

```json
{
  "ts": 1794358800000,
  "values": {
    "schema_version": 1,
    "boot_id": "00000000000000000000000000000006",
    "record_seq": 10,
    "record_kind": "TRANSITION",
    "replayed": false,
    "time_synced": true,
    "captured_uptime_ms": 60000,
    "risk_level": "NORMAL",
    "risk_reason": "NORMAL_SAFE",
    "monitoring_status": "OK",
    "emergency_latched": false,
    "physical_ack_seq": 1,
    "buzzer_mode": "OFF",
    "buzzer_output_on": false,
    "muted": false,
    "mute_remaining_s": 0,
    "recovery_remaining_s": 0,
    "gy906_quality": "VALID",
    "mq2_quality": "VALID",
    "flame_quality": "VALID",
    "radar_quality": "VALID",
    "overall_quality": "OK",
    "radar_source": "UART",
    "temp_object_c": 25.4,
    "gas_detected": false,
    "flame_detected": false,
    "presence": true,
    "detection_state": "NO_EMERGENCY",
    "ack_eligible": false
  }
}
```

Case6 có thể về NORMAL ngay sau ACK nếu engine hiện tại đủ điều kiện; không bịa một RECOVERY level. Với WARNING recovery, gửi WARNING + reason RECOVERY_PENDING + recovery_remaining_s, sau đó NORMAL_SAFE. `EMERGENCY_ACKNOWLEDGED` có thể bị ghi đè ngay nên physical_ack_seq cần capture ở branch clear, không chỉ nhìn reason.

### 7. Boot / reconnect chưa đồng bộ clock

BOOT chưa có measurement, không bịa trạng thái đã evaluate:

```json
{
  "schema_version": 1,
  "boot_id": "00000000000000000000000000000007",
  "record_seq": 0,
  "record_kind": "BOOT",
  "replayed": false,
  "time_synced": false,
  "captured_uptime_ms": 250,
  "reset_reason": "POWERON"
}
```

Authoritative resync sau evaluation, cùng boot_id, không native ts khi clock chưa sync:

```json
{
  "schema_version": 1,
  "boot_id": "00000000000000000000000000000007",
  "record_seq": 1,
  "record_kind": "RECONNECT",
  "replayed": false,
  "time_synced": false,
  "captured_uptime_ms": 60000,
  "risk_level": "NORMAL",
  "risk_reason": "NORMAL_SAFE",
  "monitoring_status": "OK",
  "emergency_latched": false,
  "physical_ack_seq": 0,
  "buzzer_mode": "OFF",
  "buzzer_output_on": false,
  "muted": false,
  "mute_remaining_s": 0,
  "recovery_remaining_s": 0,
  "gy906_quality": "VALID",
  "mq2_quality": "VALID",
  "flame_quality": "VALID",
  "radar_quality": "VALID",
  "overall_quality": "OK",
  "radar_source": "UART",
  "temp_object_c": 25.4,
  "gas_detected": false,
  "flame_detected": false,
  "presence": true,
  "detection_state": "NO_EMERGENCY",
  "ack_eligible": false
}
```

Client attributes (không phải telemetry):

```json
{
  "schema_version": 1,
  "firmware_version": "pccc-v1.0.0-proposed",
  "build_id": "example-reviewed-build",
  "hardware_model": "ESP32-S3 N16R8",
  "zone_id": "V1",
  "config_id": "example-reviewed-safety-config",
  "capabilities": [
    "mlx90614",
    "mq2_do",
    "mq2_ao",
    "flame_do",
    "flame_ao",
    "ld2410",
    "physical_ack",
    "buzzer",
    "rgb",
    "oled"
  ]
}
```

## 6. Publish policy, buffering và ordering

### Sampling khác publish

Không đổi sensor sampling/evaluation vì Cloud optimization. Proposed:

| Stream | Trigger | QoS | Chính sách |
|---|---|---|---|
| Full state transition | Risk/reason/monitoring/latch/accepted ACK/mute thay đổi | 1, retain0 | Enqueue ngay sau local decision/actuation; wake network, không chờ periodic tick |
| Measurement snapshot | NORMAL10s; WARNING/EMERGENCY2s | 1 ở V1 nhỏ | Latest-only queue, không backlog mọi sample; coalesce nếu mạng chậm |
| Ambient | 30s hoặc cùng abnormal context | 1 | Không cần mỗi sample1s |
| Device health | 60s; drop/memory fault on change | 1 | Có thể gộp snapshot, không flood từng allocation |
| Heartbeat | 30s nếu không publish khác | 1 | Boot/time/seq+uptime, không giả measurement mới; tránh duplicate heartbeat riêng khi đã có traffic |
| Boot/reconnect | Mỗi boot/CONNECTED + evaluated full resync | 1 | Attributes nếu đổi, current snapshot trước backlog |
| Engineering ADC/radar |10s khi explicit engineering profile | 0 hoặc1 theo profile | Không kéo diagnostics vào production default; ưu tiên safety state |

Dự toán để chọn quota, không phải measurement CPU/network: payload live flat khoảng0.4–1.2KB tùy context. 1KB mỗi10s ~8.6MB/day/device payload, so với1KB mỗi1s ~86MB/day; TLS/MQTT overhead chưa cộng. State/health bổ sung; ThingsBoard có thể tính usage theo datapoints/message/chunks tùy plan. **Tenant quota/plan UNKNOWN**, WP03 phải đo bytes và số key, không coi thông số này là cam kết chi phí. QoS0 cho trend có thể giảm load sau khi đo, không bắt buộc thay QoS1 ngay.

### Queue ownership và minimum bounded memory

Giữ queue latest1 cho periodic snapshots. Event32 là điểm khởi đầu đủ cho V1 bench, không vô hạn; define policy rõ: reserve capacity cho critical, coalesce repeated identical state, counter mọi drop/eviction, critical overflow dẫn tới loss marker và latest authoritative critical resync, không log “đã lưu” nếu thất bại. Không dùng memory growth để hứa giữ vô hạn khi mất mạng.

Claim immutable event theo ID; network chỉ xóa đúng ID khi PUBACK ứng với publish đã biết. Không mutex quanh TLS/socket. Bounded in-flight (đề xuất tối đa4 event), outbox byte cap ban đầu16KiB rồi đo TLS/internal heap để chỉnh; nếu thiếu RAM hạ xuống8KiB. Đây là budgets đề xuất cho WP04, không bật PSRAM. QoS1 là at-least-once, không exactly-once; PUBACK chứng minh broker nhận, **không** chứng minh Rule Chain/database đã commit. Cross-power-loss durable events chỉ đảm bảo nếu persistence policy được duyệt; broker/outbox retries không đủ.

Chọn một reconnect owner (library hoặc explicit app scheduler), bounded exponential backoff+jitter1→60s; CONNECTED notification đánh thức publish ngay. Không gọi publish từ sensor task. Critical wake/bounded enqueue phải có test local deadline; event được tạo khi detector chạy, không hứa phát hiện nhanh hơn sensor loop.

### Ordering và dedup

Identity immutable: authenticated device originator + boot_id + record_seq. MQTT msg_id không dùng làm application ID (reuse theo session). Sequence chung mọi record, snapshot/event share cùng ordering domain. Thứ tự priority queue không phải chronological; store occurrence uptime/epoch riêng.

Rule Chain phải chặn stale trước **Save Timeseries và alarm update**:

1. Xác thực schema/type, lookup current accepted boot+seq bằng processor có per-device serialization/atomic state (dedup qua server attributes đơn giản không đủ nếu xử lý song song).
2. BOOT/RECONNECT live mở current boot session; full resync current state phải đến trước backlog. On same boot chỉ current record_seq lớn hơn accepted high-water mới update current state. New boot chưa xác thực session/first-connect context không được tự ghi đè chỉ vì chuỗi boot_id khác.
3. TRANSITION luôn archive immutable `safety_event` một lần với event identity; replayed/history-only branch không lưu `risk_level`/latch dưới current keys và không clear current alarms.
4. Full live state mới đủ để create/update/clear alarms. Delayed event cũ không được tạo alarm mới như hazard vừa xảy ra sau khi current resync đã NORMAL; nó là historical incident cần review.
5. Cùng timestamp ms có thể nhiều records: event ID phải giữ nguyên, không dựa vào `ts` làm dedup. Time-series JSON `safety_event` cùng key/cùngms có thể bị overwrite; triển khai archive phải dùng unique storage row/event sink hoặc server-assigned increasing archive timestamp và giữ occurrence_ts bên trong. Tenant storage/Rule Chain khả năng cụ thể cần test WP05; không hứa event audit exactly-once chỉ bằng time-series key.

Chỉ dùng native telemetry envelope chưa tự thực hiện ordering/alarm policy. WP05 cần custom Rule Chain guard/archive; không phải “ThingsBoard tự làm mọi thứ”. Nếu triển khai guard phức tạp chưa cần cho lab, minimum phải replay qua keys history riêng và luôn giữ authoritative resync; không nhận historical replay vào current alarm evaluator.

## 7. Data quality, timestamp và schema evolution

- Missing = value omitted vì không có sample/không có feature; INITIALIZING = sensor còn warmup; INVALID = read/validation lỗi; STALE = đã có valid nhưng quá timeout; VALID không tự chứng minh physical connectivity.
- Không gửi NaN/Infinity; reject/omit mọi nonfinite trước JSON. Không dùng0 làm placeholder phép đo. Không phụ thuộc null để “xóa latest” trên ThingsBoard: omit measurement và gửi quality rõ ràng; dashboard **bắt buộc mask latest value cũ** nếu quality không VALID hoặc device inactive/age quá hạn. `null` ingestion khác version/transport cần kiểm tra, không dùng làm minimum contract.
- Khi gửi AO thật, dùng AO validity riêng; DO quality không bảo đảm AO hợp lệ. Nếu engineering AO được bật cần key quality/source phù hợp hoặc record context; không tái sử dụng mq2_quality để khẳng định calibration.
- Capture timestamp lúc lấy mẫu/evaluate, không lúc network serialize. Current pipeline last_update đánh dấu attempt; cần last_success/source stamp từ driver/pipeline cho age. Không gán giá trị giữ lại từ trước outage như sample mới.
- SNTP sync status callback + kiểm tra clock sane/fresh; timezone không ảnh hưởng epoch. Nếu device clock chưa đáng tin, flat payload dùng server receive time. Gửi captured_uptime_ms để giải thích đây là receive-time timestamp, không pretend occurrence epoch.
- Offline same-boot nếu có clock anchor tin cậy và không clock jump: có thể tính occurrence epoch bằng anchor - delta monotonic; đánh dấu reconstructed trong archive metadata. Cross-boot/unknown clock không đủ anchor: giữ occurrence_uptime/boot_id và receive time, không bịa epoch1970 hay dùng uptime as ts. Không đưa reconstructed times vào current state ordering thay seq.
- Clock step SNTP có thể làm epoch đi lùi: seq/boot vẫn dùng ordering; keep occurrence and receive timestamps; không tự clear alarm dựa solely timestamp.
- Compatibility: `schema_version=1` là **contract mới**, không phải old firmware V1 sản phẩm. Legacy không có version route riêng. Thêm optional key giữ version; đổi đơn vị/type/semantics/remove key cần version2. Không chuyển một key từ pseudo ADC sang ADC thật tại chỗ.
- Export release version/config hash bằng client attrs; mỗi record vẫn có schema_version để parser không phụ thuộc attribute arrival order.

## 8. Alarm lifecycle trên ThingsBoard

Risk Engine quyết định rủi ro. Firmware báo full state/transition. Cloud quản lý workflow/visualization, không tính lại điều kiện sensor hoặc ACK vật lý.

[ThingsBoard alarm model](https://thingsboard.io/docs/paas/user-guide/alarms/) có hai trục active/cleared và acknowledged/unacknowledged; acknowledge là operator đã xem, clear là condition kết thúc. Một active alarm theo originator+type được update thay vì tạo bản mới; clear không xóa lịch sử.

| Input authoritative | Cloud action đề xuất |
|---|---|
| WARNING | Create/update `PCCC_WARNING`, severityMAJOR, detail risk_reason + quality + record identity |
| EMERGENCY hoặc emergency_latched=true | Create/update **một** `PCCC_EMERGENCY`, CRITICAL; không tạo alarm type riêng cho mỗi reason |
| EMERGENCY_LATCHED | Giữ cùng emergency alarm active, detail phase=LATCHED, physical latch true; dù detection đã clear vẫn không clear Cloud condition |
| Recovery pending | Update detail/countdown; giữ alarm của risk còn active |
| Physical latch cleared và newer state non-EMERGENCY | Clear `PCCC_EMERGENCY`, lưu clear cause/physical_ack_seq hoặc boot-policy annotation; không gọi physical ACK ngược |
| WARNING→EMERGENCY | Clear warning với reason escalated, create/update emergency; không gọi đó là recovered NORMAL |
| WARNING→NORMAL/SUSPICIOUS với latchfalse và authoritative newer state | Clear warning (reason dropped-below-warning); NORMAL transition lưu event, không tạo NORMAL alarm |
| MONITORING_FAULT | Create/update `PCCC_SENSOR_FAULT`, MAJOR; có thể cùng emergency; không override severity emergency |
| FAULT→OK/DEGRADED | Clear sensor fault, lưu recovery event; DEGRADED vẫn badge health, không tạo nhiều alarm nhỏ mặc định |
| Device inactive | Separate `PCCC_DEVICE_OFFLINE`, MAJOR; không kết luận risk NORMAL, không clear emergency |
| Resync sau reboot | Giữ unresolved emergency cho tới full authoritative snapshot; xử lý theo persistence policy đã duyệt, annotate reboot, không âm thầm gọi physical_ack=true |

Cloud ACK chỉ thay operator workflow, **không** đổi physical_ack_seq/latch/buzzer. RBAC/dashboard không cho operational users clear active emergency tùy ý; nếu administrator override Cloud record, vẫn hiển thị latest device latch và tạo lại/update theo authoritative state. Cloud clear không thể ngăn còi thiết bị.

Không reset history khi reason thay; dedup bằng device+type cho alarm, boot+seq cho event. Concurrent multiple sensor reasons dùng engine primary; quality/fault riêng. Event archive delayed/replayed không tự gửi notification như incident hiện tại; đánh dấu delayed và review lịch sử.

Device offline dùng ThingsBoard state service; heartbeat30s và proposed inactivityTimeout90000ms. Không tự phát telemetry `device_online=false` từ thiết bị đã mất mạng. [Official inactivity recipe](https://thingsboard.io/docs/recipes/device-inactivity-alarm/) mô tả server active/inactivity events và per-device timeout; Cloud tenant entitlement/available UI cần WP05 xác minh.

## 9. Dashboard information architecture

### Operational View

- Device/zone identity, last seen và connectivity; stale/inactive overlay rõ ràng.
- Current risk lớn, primary reason tiếng Việt dễ hiểu; latch badge độc lập, physical ACK eligibility chỉ hướng dẫn giữ nút tại chỗ.
- Active alarms và action ngắn; không nút remote mute/ACK vật lý/threshold tuning.
- Temperature object trend có quality gaps, không nối thẳng qua invalid/outage như dữ liệu hợp lệ.
- Gas/smoke badge từ DO; trend ADC chỉ engineering có unitscounts/mV và ghi chưa hiệu chuẩn. Không gauge ppm.
- Alarm history/incident timeline; device inactivity không được làm risk card xanh.

### Engineering View

- Sensor quality theo từng sensor, UART/OUT source, timestamp trust và boot/record ordering.
- Object/ambient/dT context; AO counts/mV tùy profile; raw DO, radar UART metrics chỉ khi debug.
- Buzzer mode vs output phase, latch state, applied physical_ack_seq và recovery/mute countdown.
- Heap/minimum/drop counters, reset reason, release/config/schema identity; broker ACK không gọi là DB commit.
- Historical delayed events tách khỏi current state, filter session/boot; full sensor debug logs không lặp toàn bộ OLED dashboard.

## 10. Compatibility, migration và security boundary

1. Export/backup tenant Rule Chain/dashboard hiện có trước thay đổi (sau duyệt, chưa làm vòng review).
2. Test-device profile nhận fixtures của old và schema1; legacy branch flatten có explicit mapping. **Không** rename MQ2_raw thành mq2_adc_counts; legacy phải gọi mq2_do_index hoặc giữ diagnostic-only.
3. Create schema1 keys bên cạnh old, xem ingestion scalar/type/ts trong tenant. Production switch theo device cohort; không dual-send vô hạn.
4. Update dashboard/alarms dùng new keys với quality/time/ordering guard, rồi tắt legacy publisher. Giữ historical old series để phân tích, không destructive backfill.
5. Rollback firmware phải route về legacy branch, không để schema1 latest values giả mới; version/capabilities indicate compatibility.

V1 đề xuất **read-only**: không shared safety attributes, không RPC subscriptions. Metadata/server labels không là lệnh. Remote configuration nếu sau này chỉ cho publish interval/log verbosity giới hạn, cần authenticated scoped command, bounds validation, version/expiry, rollback và audit; không fire thresholds, emergency disable, latch clear hay emergency mute.

Credentials không nằm trong payload hay client attributes. Token scoped từng device, TLS CA/hostname và date validation được test; provisioning/secrets theo audit R05/R15. Broker URI tự chọn phải được xác nhận tenant/endpoint path trước migration; không thay host đang chạy chỉ vì docs có hostname Cloud mẫu.

## 11. Acceptance gate cho contract

- Seven scenario groups validate JSON Schema + semantic quality rules, ingestion qua đúng API vào test tenant; measurements/units không giả.
- FIRE event không phụ thuộc10s trend timer và không block local actuation.
- PUBACK retry/duplicate/out-of-order/backlog/reset đồng thời không clear latch/alarm sai, không mất event không có counter.
- Sensor invalid không hiển thị last valid như current; unsynced event không bị đặt vào1970.
- Cloud ACK không có network path tới physical ACK. Không thay đổi tenant trong vòng review này.
