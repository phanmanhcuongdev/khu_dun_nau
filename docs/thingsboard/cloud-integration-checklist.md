# PCCC V1 — Cloud integration acceptance checklist

[← Mục lục tài liệu](../README.md) · Chuẩn hóa tài liệu: **2026-11-12**.

**Trạng thái: tham chiếu contract firmware V1.** Đọc [hướng dẫn tích hợp](../guides/thingsboard.md) trước; các bước live Cloud chưa đánh dấu hoàn thành phải được nghiệm thu riêng trên tenant được cấp quyền.

Đồng hành với [MQTT handoff](mqtt-data-contract.md), CSV dictionary và JSON examples. Firmware1.1.0-phase1 / schema1. Mọi setup/test live dưới đây là **RECOMMENDED — NOT IMPLEMENTED / NOT VERIFIED** trong repository; ô trống không là PASS. Chỉ thực hiện với tài khoản/tenant/device test được phép. Không dùng device token để suy quyền admin, không publish fixtures vào production, không kích thích lửa/khói thật.

## 1. Chuẩn bị dữ liệu và quyền

- [ ] Ghi tenant/device originator/profile/environment, quyền read telemetry và quản trị rules/dashboard được cấp; không ghi token vào biên bản.
- [ ] Xác minh broker iot.cuongdso.id.vn:8883 ánh xạ tới đúng device/tenant dự kiến. MQTT CONNECTED/PUBACK chỉ là transport evidence.
- [ ] Lấy backup/version Cloud config hiện có trước thao tác của người phụ trách; kiểm existing normalization/legacy routes, không xóa telemetry/alarm history.
- [ ] Parse PCCC_V1_PAYLOAD_EXAMPLES.json; chỉ entry.payload là wire payload. Tách attrs topic; dùng examples độc lập, không coi same boot/seq10/ts là trace. Reset test originator dedup/order baseline giữa independent cases; replay case giữ same ID có chủ đích.
- [ ] Ghi nhận caveats SENSOR_FAULT/BOOT aggregate fixture defaults, attribute fake versions; không sửa firmware để hợp thức hóa fixture. Không dùng fake build/config IDs làm deployed expected values.

## 2. Ingest/type/time/quality

- [ ] Kiểm một actual message đã có trong tenant: schema_version integer1, record_kind string, numeric/bool đúng CSV; không convert false thành missing/string hoặc null thành0. Không chỉ nhìn broker PUBACK.
- [ ] Xác minh cả hai topics: telemetry -> time-series, attributes -> client attributes; có9 attributes, không biến thành shared configuration.
- [ ] Kiểm normal payload native ts milliseconds (ví dụ fixture1794358800000), không seconds1794358800; values là flat scalar. Kiểm ingress đã unwrap hay chưa, không unwrap lần hai.
- [ ] Kiểm unsynced sample không có ts/values wrapper, time_synced=false và server receive timestamp; captured_uptime_ms không được dùng làm Unix time. MQTT gate có trusted clock không làm historical capture tự thành synced.
- [ ] Kiểm temp_object_c/ambient_c là °C filtered với nhãn khác nhau; temp_rate_cps là °C/s; không đổi nguồn/threshold trên Cloud.
- [ ] Kiểm GY quality INVALID/STALE/INITIALIZING hoặc missing temp -> placeholder/no new point; Latest old value không được tô như hiện tại. Sử dụng capture/current session context, không dùng metadata HEARTBEAT để freshen risk/sample.
- [ ] Kiểm gas_detected/flame_detected/presence là booleans; MQ2 warmup có thể omit gas; không hiển thị raw DO nếu firmware không gửi; quality VALID không là physical connection certification.
- [ ] Nếu có actual AO data: mq2_adc_counts integer0..4095, mq2_adc_mv integer>=0 khi calibration valid; tách mq2_ao_valid/mq2_ao_mv_valid khỏi DO quality; không ppm. Fixtures không cung cấp AO/rate values, không bịa giá trị để báo ingestion PASS.
- [ ] Không chờ legacy MQ2_raw/flame_raw/radar_distance hoặc14 schema-only event-health keys. Dictionary thực tế có50 current/health,34 history,9 attrs; ts/values không tính là telemetry fields.

## 3. Routing/order/dedup

- [ ] Validate schema_version/record_kind trước persistence/alarm route; unknown/legacy không default NORMAL.
- [ ] Route bằng record_kind, không chỉ startsWith(event_): HEARTBEAT có6 counters event_* vẫn health-only.
- [ ] Current BOOT/RECONNECT/SNAPSHOT được xử lý serial theo originator; seq chỉ order trong cùng boot. Cùng seq duplicate/older không rollback. New boot/session được xác minh bằng ingress/session boundary, không sort random boot IDs.
- [ ] Chạy NORMAL current case trong isolated test context, xác nhận current risk NORMAL. Chạy từng WARNING/EMERGENCY current projection ở context riêng; không lấy TRANSITION event_* làm current alarm input.
- [ ] Từ accepted current NORMAL/newer state, đưa historical EMERGENCY event vào archive route: current risk/latch/boot/seq không đổi, không tạo lại live emergency. Thử cả event_replayed=false và true.
- [ ] Gửi lại same transition identity (originator+event_boot_id+event_record_seq+TRANSITION) qua test ingress: chỉ một archived event; chưa có duplicate alarm. Test out-of-order events không đổi current.
- [ ] Snapshot và transition cùng capture ID không loại bỏ nhau do dedup sai stream. Test hai distinct events có cùng ts vẫn giữ đầy đủ archive entries.
- [ ] HEARTBEAT record_seq lớn hơn current không advance risk high-water; subsequent valid current vẫn được nhận. Không tạo coherent snapshot bằng cách ghép Latest keys từ nhiều streams.
- [ ] Sau reconnect current resync lấy làm latest authoritative trước khi history drain; không coi enqueue order/PUBACK là DB transaction.

## 4. Alarm semantics/read-only

- [ ] NORMAL/SUSPICIOUS/WARNING/EMERGENCY hiển thị theo risk_level enum thật, primary reason đúng field; không reimplement gas/temp classifier hoặc mặc định WARNING=gas/EMERGENCY=flame.
- [ ] EMERGENCY_LATCHED không là risk_level mới: latch true giữ emergency incident ngay cả detection NO_EMERGENCY; UNKNOWN không được tô safe. ack_eligible chỉ hướng dẫn thao tác vật lý, không phải ACK accepted.
- [ ] Sensor fault/stale indicator từ monitoring_status/per-sensor quality có thể cùng emergency; không ghi đè/cancel fire vì fault hoặc mất mạng. Fixture aggregate OK không phủ nhận per-sensor INVALID.
- [ ] physical_ack_seq chỉ so với baseline được xác minh; post-ACK recovery case không chứng minh new ACK nếu chưa biết trước đó. Reason EMERGENCY_ACKNOWLEDGED transient có thể bị bỏ lỡ nhưng current clear+seq mới vẫn phản ánh trạng thái.
- [ ] Một emergency active/device/type, snapshots/retries/latched phase update cùng incident; recovery không delete lịch sử. Không thêm alarm mới chỉ vì lặp message.
- [ ] Nhấn Cloud Alarm ACK trên test alarm: không tạo RPC/device command, không thay physical_ack_seq/latch/buzzer. Cloud acknowledgment và physical clear độc lập.
- [ ] Nếu current authoritative latch cleared, áp dụng policy clear incident mà vẫn giữ Cloud acknowledgment/history; không clear từ historical replay hoặc device offline.
- [ ] Không tạo nút remote mute/clear latch/threshold config, RPC safety control hoặc shared-attribute listener; firmware hiện không subscribe các chức năng đó.

## 5. Offline/operations/evidence

- [ ] Cấu hình server inactivity90s cho đúng profile/device với quyền phù hợp rồi xác minh trên môi trường test. recommended_inactivity_ms client attribute không tự làm việc này; không LWT/online bool của firmware.
- [ ] Kiểm offline incident độc lập, không clear active emergency hoặc đổi detection thành NO_EMERGENCY. Sau reconnect giữ history/ACK provenance.
- [ ] Chia Operational/Engineering Views theo handoff; event archive không là current tile source; labels ADC pin mV, object/ambient, timestamp trust và sensor quality rõ ràng.
- [ ] Counters dùng boot_id để reset baseline; event_expiry_count không nghĩa TTL drop, event_drop_count có overlap, journal4 copies không full database và PUBACK không DB commit.
- [ ] Ghi evidence từng case: environment/device originator, input example ID hoặc capture reference, received time/capture time, accepted boot/seq, dedup decision, alarm before/after, expected/actual, PASS/FAIL/NOT VERIFIED. Không lưu credentials/screenshots chứa token.

## Kết quả validation của tác vụ documentation

| Kiểm tra documentation | Kết quả |
|---|---|
| Source-key extraction -> CSV | **PASS** —93 category rows:50 current/health,34 historical,9 client attributes;84 distinct telemetry keys |
| Enum/type/unit/source references | **PASS** — Risk/Reason/Monitoring enums đối chiếu source; °C, °C/s, ADC counts, ADC-pin mV, ms/µs/bytes và booleans đúng contract |
| JSON examples | **PASS** —16 entries parse + Draft2020-12 + exact serializer key-shape/quality checks; provenance của10 verbatim fixtures,5 prefix projections và1 replay-flag projection khớp |
| Markdown JSON blocks | **PASS** —12 blocks parse và validate đúng topic schema |
| References/table coverage | **PASS** —38 Markdown file/line links tồn tại; toàn bộ CSV keys có trong bảng bàn giao |
| Known discrepancies | **RECORDED** —14 schema-only event-health keys, fixture fake versions/default quality và same-ID cases, counter semantics; không sửa source artifacts |
| Credential scan | **PASS** — Không có literal SSID/password/device token hiện cấu hình trong4 deliverables; values không được in vào kết quả |
| Scope before/after hashes | **PASS** — Chỉ thêm4 files docs/thingsboard; firmware/CMake/Kconfig/sdkconfig/schema/fixtures và tài liệu cũ giữ nguyên |
| Live Cloud acceptance | **NOT VERIFIED** — Không đọc/sửa tenant, không tạo Rule Chain/Dashboard/Alarm |

Các PASS ở đây chỉ xác minh tài liệu. Evidence firmware/HIL TESTED tham chiếu báo cáo Phase1, không phải build/test hardware mới.

Các check live ở trên vẫn trống/NOT VERIFIED. Không build/flash/fault injection hoặc thao tác Cloud trong tác vụ documentation.
