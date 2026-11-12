# PCCC V1 — Production Phase 1 implementation

[← Mục lục tài liệu](../README.md) · Chuẩn hóa tài liệu: **2026-11-12**.

**Trạng thái: báo cáo triển khai lịch sử.** Kết quả build/flash/test bên dưới được ghi tại thời điểm thực hiện; không tự áp dụng cho mọi checkout sau này. Log, manifests, patches và thư mục evidence được giữ cục bộ, không phát hành trong repository public. Các mô tả “chưa có Git” phản ánh lúc viết báo cáo.

Ngày: 2026-11-11. Baseline: ba review trong `docs/reviews/` và `wp01-implementation-report.md`; D1–D4 giữ nguyên. Phạm vi firmware WP02, WP03, WP04 và WP06. Không thực hiện WP05/dashboard, Cloud migration hoặc thay đổi safety thresholds.

## 1. Kết quả và phạm vi nghiệm thu

Đã triển khai event ownership/PUBACK lifecycle, bounded critical journal, serializer/schema V1, publisher độc lập sampling, private provisioning và TLS/startup hardening. Host regression và build ESP-IDF 6.1 PASS. Đã flash thiết bị xác định đúng MAC, kết nối TLS/MQTT thực và nhận PUBACK. Chi tiết bản cuối và số đo tại mục 8, evidence kèm theo.

Đây là hoàn tất implementation firmware cùng những kiểm thử được liệt kê, **không phải chứng nhận production readiness toàn bộ hệ thống**. Live tenant ingestion/Rule Chain/Cloud alarm chưa được xác minh; fault injection trên host không thay thế real TCP/RF outage, power-cut hoặc soak test.

Không có Git metadata hoạt động; không khởi tạo repository. Backup trước sửa: `/tmp/phase1-baseline/`, directory0700, chứa source gốc có credentials nên chỉ giữ riêng. Manifest trước/sau, redacted diff, checkpoint theo workstream và evidence công khai tại `docs/production/evidence/phase1/`. Không đưa private provisioning hay binary chứa credentials vào các artifacts này.

## 2. Changed files và checkpoints

| Nhóm | File/phạm vi | Nội dung |
|---|---|---|
| WP02 | `main/include/event_buffer.h`, `main/src/event_buffer.c` | Static32 slots, immutable identity, claim/remove exact ID, priority, counters |
| WP02 | `main/include/mqtt_delivery.h`, `main/src/mqtt_delivery.c` | Four-slot delivery ledger, epoch/MID correlation, timeout, reuse quarantine |
| WP02 | `main/include/critical_journal.h`, `main/src/critical_journal.c` | Four NVS critical records, version/CRC/readback, bounded persistence |
| WP03 | `main/include/cloud_record.h`, `main/include/cloud_codec.h`, `main/src/cloud_codec.c` | Capture snapshot và allocation-free bounded serializer/attributes/health |
| WP03 | `schemas/pccc-v1/`, `fixtures/pccc-v1/`, `tools/generate_contract_assets.py` | Versioned dictionary, strict JSON Schemas, 10 fixtures gồm 7 nhóm tình huống yêu cầu |
| WP04 | `main/src/tb_mqtt.c`, `main/include/tb_mqtt.h`, `main/include/publish_policy.h`, `main/include/clock_policy.h` | Sole network owner, cadence, SNTP trust gate, resync/history, bounded retries |
| WP06 | `main/src/wifi_app.c`, `main/include/wifi_app.h` | Private credentials, RAM Wi-Fi config, fail-soft init/cleanup, one retry owner |
| Integration | `main/main.c` | Local alarm/persistence handoff trước cloud enqueue; network init failure không abort; timing instrumentation |
| Build/security | `main/CMakeLists.txt`, `main/include/diagnostic_config.h`, `sdkconfig`, `.gitignore` | Private generated header, source/config fingerprints, production diagnostics off, certificate dates và deleted-message notices enabled |
| Provisioning | `tools/generate_build_config.py`, `tools/provisioning.example.json`, `tools/PROVISIONING.md`, `tools/release_check.py` | Buildable local config/sample, input bounds, redacted release checks |
| Tests | `tests/phase1/` | Production-source fault injection, concurrency, schema/semantic và routing tests |
| Phase 2 | `migration/phase2/` | Dry routing reference, migration/rollback/validation handoff; không upload Cloud |
| Reports | Báo cáo này và `docs/production/evidence/phase1/` | Results, hashes, per-workstream patch/checkpoint, sanitized logs |

Hash baseline xác nhận **không đổi** Risk Engine, WP01 latch store, touch/ACK, Local Alarm Controller, buzzer, acquisition/pipeline, safety_config, board_pins, OLED V2, RGB, dependency lock và partition configuration. Trong `sdkconfig` chỉ đổi `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` và `CONFIG_MQTT_REPORT_DELETED_MESSAGES=y`. PSRAM vẫn disabled; không thay GPIO/eFuse. RGB self-test vẫn off, RGB R39/G38/B48 vẫn enabled.

## 3. WP02 — Event protocol/invariants

### Ownership và lifecycle

Identity là `(boot_id, record_seq)` từ lúc capture; không đổi khi retry. `boot_id` là 128-bit random theo boot; sequence dùng chung miền snapshot/event/health, cho phép có khoảng trống. `record_kind` xác định stream; một transition và current snapshot có thể cùng capture identity, nên archive dedup phải bao gồm stream/type cùng originator.

1. Safety loop hoàn tất Local Alarm Controller trước khi giao snapshot/event; không gọi socket/NVS từ đường cloud handoff này.
2. `event_buffer_push` nhận bản sao vào RAM, không heap allocation, không chờ semaphore vô hạn. Critical section chỉ scan/copy tối đa32 slots.
3. Consumer claim atomically, nhận bản sao immutable; không giữ lock khi encode/enqueue/publish.
4. MQTT enqueue trả message ID nghĩa là **accepted**, chưa là delivered. Event vẫn tồn tại.
5. Ledger correlate epoch+MID với event identity. Chỉ matching `MQTT_EVENT_PUBLISHED` (QoS1 PUBACK) mới complete/remove đúng event ID, sau đó clear đúng journal record.
6. Duplicate/out-of-order/old-epoch ACK không remove event khác. Bitmap8KiB cấm MID tái sử dụng trong một client lifetime. Gặp reuse thì recycle/destroy client và outbox cũ, retry pending IDs trong epoch mới; clean session giữ false cho disable_clean_session, tức clean session được bật.
7. Disconnect, notice overflow, deleted message, clock loss/jump hoặc delivery attempt quá35s -> release/retry ownership, recycle client. Event không bị xóa chỉ vì retry/expiry. QoS1 có thể duplicate; không hứa exactly-once hay ThingsBoard database durability từ PUBACK.

Regression cũ: INFO peek -> insert EMERGENCY -> drop_top hoàn tất INFO nhưng xóa EMERGENCY. Đã compile source baseline và tái hiện FAIL; implementation mới complete INFO bằng identity, giữ nguyên EMERGENCY. Evidence: `R02_baseline_reproduction.log` và `host_phase1.log`.

### Capacity/loss policy

- RAM32, maximum claimed4; transport maximum in-flight4 **cho tất cả streams**, outbox16KiB. Static ledger/queue không tăng theo thời gian outage.
- Khi đầy, chỉ evict unclaimed record ưu tiên thấp hơn. Không evict claimed hoặc cùng-priority critical để nhận record mới; full-critical buffer reject có counter. Safety handoff vẫn cập nhật latest snapshot dù event bị reject.
- `previous` chỉ advance khi event được queue nhận; rejected transition được thử lại ở vòng safety tiếp theo. Latest-current resync là đường riêng, không chờ drain backlog.
- Counters phân biệt created/queued/accepted/completed/retry/rejected/evicted/expired/duplicate nội bộ; health export các counters vận hành cần thiết. Các counters RAM reset theo boot. Rejection/retry là số lần thử, không phải số incident duy nhất.
- `event_expiry_count` là delivery attempts hết hạn/recycle, không phải critical history bị TTL-delete. Không có TTL xóa critical records. `event_drop_count=rejected+evicted+journal_loss` là aggregate exposure indicator có thể overlap, không phải số sự kiện mất duy nhất.

### Bounded durable critical journal

Network owner ghi namespace `pccc_events`, keys `ev0..ev3`, tối đa4 critical copies chưa PUBACK; độc lập namespace `pccc_latch/state` của WP01. Không ghi sensor samples hay mỗi evaluation. Chỉ critical transition cần durable copy; không biến thành full incident database. Duplicate identity không ghi lại. Có thể tối đa SET+completion erase mỗi critical record, cùng NVS GC; wear budget phụ thuộc số transitions, cần đo workload dài hạn, không giả định flash endurance cụ thể.

Record gồm header56bytes: magic/version, boot ID, sequence7bytes (JSON-safe integer cap), payload length, CRC32 bảo vệ identity và JSON. Payload tối đa2304bytes; explicit byte encoding, không serialize padding C struct. Commit và read-back/integrity validation trước đánh dấu durable. Bốn record tối đa9440bytes dữ liệu ứng dụng, chưa gồm NVS overhead/GC. Shared NVS partition không được resize/erase.

Boot reload pending records. Corrupt/unreadable slots được quarantine, log/counter và không tự overwrite; unavailable backend không dừng local alarm. Full journal không evict pending critical; durable-copy rejection được đếm, RAM vẫn giữ theo budget. Sau PUBACK giải phóng chỗ, rescan những RAM critical còn pending. Write/commit/readback failures được đếm, không giả vờ durable. Lỗi uncertain write có thể đã lưu dữ liệu trên flash: reboot có thể replay duplicate, được identity/dedup xử lý.

Giới hạn: mất nguồn trước async journal commit có thể mất event; full journal chỉ bảo toàn tối đa4 outstanding records; noncritical RAM-only history mất qua reboot. Accepted physical ACK transition khi đã clear latch có priority INFO nên timeline đó RAM-only; physical_ack_seq và trạng thái clear vẫn persist theo WP01 và được resync, không giả rằng mọi ACK timestamp đều durable. Khi broker đã PUBACK nhưng DB chưa lưu, việc clear journal vẫn có exposure theo QoS1 contract. Không power-cut HIL trong phase này. Invariants/power windows của WP01 giữ nguyên, không tuyên bố được sửa bởi event journal.

## 4. WP03 — Final Data Contract V1

Nguồn chuẩn: `schemas/pccc-v1/README.md`, `data_dictionary.json`, `attributes_dictionary.json`, `telemetry.schema.json`, `attributes.schema.json`. Fixture values là **minh họa do production serializer sinh**, không phải phép đo phần cứng.

| Stream | Dạng | Trạng thái |
|---|---|---|
| Current | `SNAPSHOT`, `BOOT`, `RECONNECT` | Flat unprefixed scalar keys, exact Risk Engine/WP01 exports |
| History | `TRANSITION` | Flat `event_*` scalar keys, immutable capture state/identity; replay transport flag có thể đổi |
| Health/heartbeat | `HEARTBEAT` | Metadata/counters/resources, không lặp lại risk hiện tại hoặc advance current-state high-water |
| Client attributes | Identity/config/capabilities/reset | Publish boot/reconnect, QoS1; không secrets/shared safety config |

**Refinement so với proposal trước release:** toàn bộ historical transition fields được prefix `event_`, còn `schema_version=1` và `record_kind` để routing. Đây là cách firmware bảo đảm historical replay không ghi đè unprefixed current risk ngay cả khi chưa triển khai Rule Chain mới. Phase 2 phải dùng final schema này, không dựa vào ví dụ event unprefixed cũ. Không tạo Risk Engine thứ hai.

`risk_level`, `monitoring_status`, `detection_state`, `emergency_latched`, `ack_eligible`, `physical_ack_seq`, `persistence_health`, commanded `buzzer_mode` và sampled `buzzer_output_on` giữ riêng. `persistence_health` là enum WP01 **PENDING0 / DURABLE1 / FAULT2**; không phải journal health. Cloud ACK không có device API. ACK seq không suy ra từ reason; chỉ đọc export WP01.

MLX90614 object/ambient là filtered values hiện có; dT/dt là derived value hiện có. Không đổi nguồn/ngưỡng. Affected measurement bị omit nếu invalid/stale/initializing/nonfinite; gas DO cũng omit khi warmup. Freshness dùng timeout hiện hữu, không sửa sensor pipeline. History đánh giá validity tại capture; current snapshot kiểm freshness tại send, không gửi giá trị cũ như mới. JSON không có NaN/Infinity. Omitted key không xóa ThingsBoard Latest; Phase 2 widgets/rules phải đọc quality và age.

`gas_detected`/`flame_detected` là DO interpretation, không ppm. Không gửi MQ2 pseudo-raw dưới tên ADC. `mq2_adc_counts`/`mq2_adc_mv` chỉ từ AO thực với validity và calibration validity độc lập. Software VALID/OUT fallback không là bằng chứng sensor còn nối vật lý. Radar source UART/OUT_FALLBACK được giữ rõ; raw diagnostic distances/energies/pseudo values không gửi định kỳ.

Client attributes gồm firmware_version, build_id, config_id, hardware_model, zone_id, capabilities, reset_reason, schema_version và recommended_inactivity_ms. Build fingerprint lấy public firmware sources/build files/sdkconfig/locked dependencies/provisioned flag; config fingerprint lấy safety_config+board_pins+sdkconfig. Không hash/expose credential values trong public metadata. Device-specific binary hash lưu trong evidence; binary/ELF vẫn private.

Fixtures có kích thước 330..999bytes (gồm newline); serializer buffer cap2304bytes. Đây là payload budget mẫu, không phải packet/bandwidth worst-case đo trên wire. Schema strict producer để bắt lỗi accidental keys/types. Consumer version-gate và bỏ qua optional extensions được kiểm soát; đổi nghĩa/unit/key breaking cần version mới. Seven scenario groups: NORMAL, WARNING gas thực có trong engine, EMERGENCY, LATCHED, SENSOR_FAULT, recovery NORMAL, boot/reconnect; thêm health/attributes fixtures. Mọi reason enum trong implementation được đối chiếu với schema.

## 5. WP04 — Publish/time/reconnect policy

| Stream | Policy đã triển khai |
|---|---|
| NORMAL measurements | 10s |
| Non-NORMAL risk hoặc monitoring không OK | 2s; bao gồm warmup/fault theo state hiện hữu |
| Risk/latch/ACK/mute/quality transition | Immediate RAM enqueue + owner wake; không đợi10s |
| Health | 60s, bounded snapshot |
| Idle heartbeat | 30s nếu không traffic; có thể dùng health frame |
| CONNECTED/reconnect | Attributes + full latest current resync trước history |

Sampling vẫn1000ms; publish cadence không điều khiển acquisition. Snapshot queue latest1, callback notices32, worker8192bytes priority2, MQTT task6144 priority3. Network callback chỉ copy notice/wake; network owner duy nhất xử lý MQTT state, publish, journal và retry. Main chỉ copy/enqueue bounded data sau local alarm.

MQTT QoS1, retain0, app owner reconnect, library auto-reconnect disabled. Bounded exponential backoff1..60s + jitter dưới1s, initial connection attempt tối đa15s, transport timeout5s. CONNECTED đánh thức worker thay vì sleep60s. Callbacks stale epoch bị bỏ; overflow chủ động recycle thay vì assume delivery. Control/latest resync được ưu tiên trước history; namespace phân tách chống backlog rollback. Stop/destroy có thể block network task theo SDK, không nằm trong safety loop; flash/NVS có thể tạm pause CPU/cache, vẫn cần đo fault envelope thực tế.

Native ThingsBoard `ts` milliseconds chỉ khi record capture có trusted SNTP anchor. Nếu chưa sync: flat object, server receive timestamp, `time_synced=false`; uptime chỉ nằm trong captured_uptime_ms, tuyệt đối không làm Unix timestamp. Historical unsynced records không được backdate bằng uptime.

MQTT TLS chỉ khởi tạo sau actual sane SNTP callback (build-minus1day .. build-plus5years), anchor dưới24h, monotonic continuity và wall-clock drift tối đa5s. SNTP chỉ configure/start sau Wi-Fi có IP để tránh API TCP/IP khi network init thất bại; retry clock bootstrap tối đa mỗi60s khi chưa trust. Sync định kỳ1h; clock mất trust/nhảy -> stop transport chờ sync, local safety tiếp tục. Không dùng build time làm giờ TLS, không bypass verification. Standard SNTP **chưa được xác thực bằng mật mã**; sanity/jump gates không bảo đảm chống time spoofing. Đây là giới hạn threat model còn lại, không hứa authenticated clock bootstrap.

## 6. WP06 — Security/startup

- Real credentials đã loại khỏi public headers, migrate nguyên giá trị sang mode0600 `private/provisioning.json`; sample không secrets. CMake generated header mode0600 dưới private build directory. Check lengths/types/NUL/size, không log values. Wi-Fi storage RAM; không erase Wi-Fi NVS cũ hay rotate token.
- SDK Wi-Fi INFO logging có SSID; production đặt tag wifi ở WARN trước init. Final boot log được kiểm raw literals trước export. Không payload/token/password logging. Secret scan public release inputs PASS; binary/ELF/backup có credentials phải private; build directory đã đặt0700 và app binary/ELF0600. Không bật encryption/eFuse nên physical extraction vẫn là giới hạn chưa được giải quyết của prototype.
- TLS root CA đang dùng được giữ; hostname verification mặc định giữ enabled; bật MBEDTLS_HAVE_TIME_DATE để verify certificate validity. Broker endpoint giữ nguyên. Không dùng insecure fallback khi SNTP/broker lỗi.
- Không subscribe RPC/shared attributes, không thêm Cloud command/ACK/threshold API. Không thay tenant, Rule Chain, dashboard hay credentials đang chạy.
- Wi-Fi init từng bước trả lỗi/cleanup; không dùng `esp_netif_create_default_wifi_sta()` vì helper chứa assert/ESP_ERROR_CHECK. NVS fail -> conservative WP01/local path, không erase; network init/queue/task failure log và local loop tiếp tục. Optional OLED/RGB init fail-soft hiện hữu giữ nguyên. Retry init sau thành công là idempotent; không hỗ trợ teardown/reconfigure Wi-Fi runtime.
- Dependency lock giữ nguyên, không upgrade SDK/MQTT/SSD1306 hay thêm runtime framework. Serializer không dùng cJSON allocation, production radar/raw diagnostic tests/logs mặc định tắt.

## 7. Regression/test matrix

| Kiểm thử | Môi trường/kết quả | Giới hạn |
|---|---|---|
| R02 baseline INFO/FIRE interleaving | Expected FAIL trên source cũ, PASS source mới | Deterministic host scheduling |
| Full32, claimed ownership, priority/reject/evict, duplicate, two producers/two consumers2000 attempts | PASS, ASan/UBSan | Không là MCU scheduler stress/soak |
| Delayed/out-of-order/duplicate/old-epoch ACK, MID reuse, cap4, timeout | PASS, real ledger | Fake broker acknowledgments |
| Journal4, duplicate/no-extra-writes, reload/delete, CRC metadata corruption, full/read/open/set/commit failure | PASS, actual journal + mock NVS | Chưa power-cut/NVS GC HIL |
| Network source scenarios0..12: delayed ACK, blackhole, disconnect trước ACK, reconnect storm, notice overflow, init/handler failure, absent clock, clock jump, full-critical current resync, enqueue rejection, Wi-Fi absent, delayed IP | PASS, actual owner + injected SDK; cap4 | Simulated90s/scenario, không live RF/TCP fault injection |
| Network queue/task allocation failures, allocation cleanup | PASS, live-allocation assertions | LeakSanitizer disabled do ptrace; không chứng minh72h leak-free |
| Wi-Fi source12 init failures/cleanup, idempotent init, RAM config, reconnect schedule | PASS, mock SDK | Chưa force actual MCU OOM |
| Codec finite/fresh/invalid/stale/warmup/AO calibration, buffer bounds, every reason enum | PASS | Không sensor calibration qualification |
| Draft2020-12 schemas +10 fixtures + negative semantic tests | PASS, jsonschema4.26 host-only | Chưa tenant ingestion |
| Migration dedup/order/history/current separation | PASS, dry-run | Không production Rule Chain implementation |
| WP01 recovery/continuous hold/unsafe ACK/persistence failure/reboot mocks | PASS | Không lặp physical ACK/power-cut mới trong phase này |
| RGB host, OLED V2 host, LD2410 tests | PASS | Không tuyên bố màu/layout/audio phần cứng được quan sát lại |
| Source/config invariant hashes + secret migration/release scan | PASS | Không bảo vệ extraction từ unencrypted binary |
| Provisioned và unprovisioned ESP-IDF builds | PASS | Không flash unprovisioned image |
| Flash/boot, actual TLS connection, broker PUBACK | PASS, thiết bị đích | PUBACK không chứng minh DB/Cloud alarm correctness |
| Wrong-host/revoked/expired/future certificate tests trên MCU | NOT TESTED | Verification enabled bằng cấu hình/code; negative staging tests còn lại |
| 24/48/72h soak, real broker blackhole/RF loss, actual journal power-cut, hardware response latency2s | NOT TESTED | Không tạo lửa/khói hoặc phá Cloud production |

Lệnh tái lập: `tests/phase1/README.md`. Evidence có kết quả từng suite. jsonschema là dependency host test trong `/tmp`, không thêm vào firmware.

## 8. Build, flash, boot và resource metrics

- **ESP-IDF6.1 build PASS**, gồm build đầy đủ1118 steps trong clean directory `/tmp/pccc-phase1-clean-build` với dependency lock giữ nguyên và rebuild final sources. Unprovisioned build cũng PASS (không flash); final provisioned image giữ credentials gốc.
- App binary **1,014,608bytes (`0xf7b50`) / 1,048,576bytes**, còn **33,968bytes (`0x84b0`, 3.24%)**. Checkpoint integrated đầu1,013,184bytes; final tăng1,424bytes sau hardening bổ sung, vẫn fit. Production diagnostics off, không cần repartition hay PSRAM.
- `idf.py size`: flash code742,262bytes; flash data160,632bytes; DIRAM143,958bytes (BSS48,784, data21,551); DIRAM report còn197,802bytes. Không suy ra free heap từ tổng linker regions.
- Network owner stack8192, MQTT6144; final HIL network unused low-water **4,872bytes**, safety/main **1,028bytes**. WP01 NVS worker3072 giữ nguyên, high-water chưa đo.
- Ba health samples: free heap **172,836 -> 177,628 -> 177,636bytes**; boot minimum **160,644bytes (~156.9KiB)**; largest block **131,072bytes (128KiB)**. Không giảm outbox xuống8KiB vì còn headroom trong workload đã quan sát; chưa có soak/OOM guarantee.
- Maximum observed safety step **3,105µs**, iteration interval **1,009,655µs**. Outbox samples3,566/605/605bytes, maximum tracked in-flight quan sát4 (host cap4 cũng PASS); không nhầm sample peak với toàn bộ transport peak.
- **Flash PASS**, hash verified; stable port `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90182195-if00` (`/dev/ttyACM0`), MAC `e8:f6:0a:8a:27:34`, ESP32-S3 rev0.2. Không erase-flash/NVS. Final image SHA256 `7872aec9765a8675f9a4ac87fd7ada28ba56d3bbc13877ec3e009d4b8f35a44d`; build_id `4dba43409b298ef3`, config_id `0e26394ea6944ef8`.
- Final boot capture180s: RGB initialized R39/G38/B48; latch restore0 / physical_ack_seq3 / DURABLE; Risk Engine UNKNOWN/warmup -> NORMAL khoảng15s; cảm biến init tiếp tục; Wi-Fi IP tại2.644s; actual sane SNTP tại36.254s; TLS/MQTT CONNECTED tại40.144s; event PUBACK seq1 và16. Không panic/brownout/reset loop trong cửa sổ này. Không có test FIRE giả từ software.
- Trong một cửa sổ trước, Cloud chưa kết nối dù local loop hoạt động. Bổ sung SNTP chờ IP/retry bounded và nonsecret diagnostics; final boot đã xác nhận recovery network trên phần cứng. Mở serial có thể làm thay đổi reset lines; boot POWERON trong log không chứng minh một power-cut test chủ động. User xác nhận có tự thử cảm biến rồi bấm nút trong thời gian thao tác, phù hợp ACK seq2 ->3; không coi counter tăng là bằng chứng auto-ACK hoặc một test ACK được agent kiểm soát.
- Final raw log **không chứa SSID/password/token**; không quan sát lại màu OLED/RGB hoặc nghe còi bằng thiết bị đo/camera. Buzzer/RGB/OLED behavior regression dựa trên host tests và invariant hashes. Chỉ có warnings runtime legacy I2C/SSD1306 có sẵn; build cảnh báo partition gần đầy, không có compiler errors/warnings mới.
- MQTT CONNECTED/PUBACK chứng minh transport thực đến endpoint hiện cấu hình; **ThingsBoard DB ingestion, tenant identity/quota, alarm rules và dashboard chưa được xác minh**.

Không erase-flash/NVS, không thay partition table. Single app1MiB giữ nguyên; chưa có OTA/rollback image slot, không claim transactional firmware rollback. Nếu phase sau vượt budget phải đánh giá tối ưu trước và migration riêng bảo toàn NVS/latch; không tự repartition.

Quan sát safety_step_max_us là từ đầu acquisition iteration tới sau Local Alarm Controller; safety_interval_max_us là khoảng giữa hai iteration starts. Chúng không đo physical stimulus-to-audible-alarm latency, không bao phủ mọi timeout/power/NVS/network faults và không chứng minh worst-case2s. Stack low-water của main khoảng1KiB cần theo dõi với fault load, chưa chỉnh task/priority safety ngoài scope.

## 9. Remaining limitations và exact Phase 2 handoff

1. **Tenant ingestion/routing:** dùng final schemas/10 fixtures; export/backup tenant config, xác định authenticated originator/session boundary và serialized per-device processing. `migration/phase2/normalize.py` chỉ là reference dry-run, chưa deployable Rule Chain. Quarantine legacy/unknown schema; không rename pseudo-raw thành ADC. Validate scalar values và native timestamp/quality omission trên test tenant.
2. **Current/history separation:** TRANSITION chỉ archive/dedup theo device+event_boot_id+event_record_seq+stream; không trigger current recovery/clear alarm. Heartbeat không advance current-state sequence. New random boot ID không có ordering tự nhiên; phải có verified session boundary, reject older seq và handle same-ms events không overwrite archive. Consumer không dùng latest record_kind làm latest risk.
3. **Alarm lifecycle/read-only boundary:** chỉ current authoritative state quyết định Cloud emergency; latch true giữ alarm, offline không clear fire. Cloud acknowledge không là physical ACK. NORMAL/recovery chỉ clear theo current firmware state và policy đã duyệt. Chống duplicate alarm/history loss; giữ incident history. Không gửi remote clear/mute/threshold commands.
4. **Server inactivity/dashboard:** cấu hình90s tại ThingsBoard theo quyền quản lý, firmware chỉ advertise recommended_inactivity_ms. Sau khi contract/ingestion/routing tests PASS mới operational/engineering dashboard; mask invalid/stale/inactive measurements, no ppm. Kiểm RBAC/read-only và migration rollback bằng tenant backup, không xóa history.
5. **Reliability qualification:** staging broker disconnect/blackhole/delayed PUBACK/auth reject/reconnect storm, wrong-host/expired/future/revoked CA negative tests, NTP absence/jumps/spoof threat, NVS full/GC và controlled power-cut journal+latch. Giữ flash/NVS footprint, measure heap/largest block/stacks và physical local latency under finite defined fault envelope. Thực hiện24/48/72h soak trước đóng băng rộng hơn supervised prototype.
6. **Operational limits:** only4 durable outstanding critical copies/32 RAM; precommit/database windows và per-boot loss counters phải được chấp nhận/hiển thị đúng nghĩa. Nếu cần history retention lớn hơn hoặc authenticated clock thì scope riêng, không giả QoS1 thành exactly-once. App còn khoảng3%, private binaries vẫn extractable, existing credentials chưa rotate. Rotation chỉ sau migration đã kiểm chứng, không phá thiết bị đang hoạt động.

Phase 2 không cần khảo sát lại serializer/enum/ownership: bắt đầu từ schemas, fixtures, data dictionary, migration README, per-workstream checkpoints và evidence của báo cáo này. Không có Cloud/dashboard thay đổi nào đã được thực hiện trong Phase 1.
