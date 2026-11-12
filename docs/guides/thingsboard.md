# Tích hợp ThingsBoard

[← Mục lục](../README.md) · Phạm vi: firmware MQTT V1 và yêu cầu diễn giải dữ liệu phía Cloud.

## Kết nối

| Mục | Cấu hình trong source |
|---|---|
| Broker | `TB_BROKER_URI` trong `main/include/tb_mqtt.h`; mặc định `mqtts://iot.cuongdso.id.vn:8883` |
| Authentication | Device access token dùng làm MQTT username, lấy từ private provisioning |
| TLS | CA nhúng `main/isrgrootx1.pem`, kiểm tra hostname và ngày chứng chỉ theo cấu hình firmware |
| Telemetry | `v1/devices/me/telemetry` |
| Client attributes | `v1/devices/me/attributes` |
| Gửi | QoS1, retain=false |
| Điều khiển từ xa | Không subscribe RPC/shared attributes/remote ACK trong triển khai hiện tại |

Nếu triển khai broker khác, thay URI và CA phù hợp, cấp đúng token rồi build lại. Tên node/zone không thay thế authenticated originator của ThingsBoard. Repository không cung cấp thông tin tenant hay quyền quản trị server.

## Điều kiện clock

MQTT chỉ được khởi tạo sau provisioning đầy đủ, Wi-Fi có IP và trusted clock. SNTP dùng `pool.ntp.org`/`time.google.com`, resync danh nghĩa 1 giờ và retry khi chưa trusted. Policy kiểm tra sane range quanh build epoch, tuổi anchor dưới 24 giờ và độ lệch wall clock với monotonic projection trong ±5 giây.

SNTP tiêu chuẩn trong firmware không được xác thực mật mã; trusted ở đây là policy kiểm tra của ứng dụng. Không bỏ kiểm tra ngày chứng chỉ hoặc gán uptime thành Unix timestamp để chữa lỗi kết nối.

## Loại record và cách sử dụng

| `record_kind` | Vai trò phía consumer |
|---|---|
| `BOOT`, `RECONNECT`, `SNAPSHOT` | Cập nhật trạng thái hiện tại sau kiểm tra session/order/quality |
| `HEARTBEAT` | Health/counters; không tự nâng current-state high-water sequence |
| `TRANSITION` | Lịch sử sự kiện; measurements và identity dùng namespace `event_*` |

Khi clock hợp lệ tại capture, payload telemetry có envelope `{ "ts": ..., "values": ... }`, `ts` là Unix milliseconds. Khi capture chưa sync, payload là flat object không có native `ts`/`values`, với `time_synced=false`; Cloud dùng receive time. Không gán timestamp giả cho record cũ sau khi đồng bộ.

Tách history khỏi current state: TRANSITION có thể replay sau reconnect, không được ghi đè live risk hoặc xóa alarm hiện tại. Dedup bằng originator + event boot ID + event record sequence. Boot ID ngẫu nhiên không dùng để sắp thứ tự các lần boot.

## Lịch gửi và giới hạn

- Current measurements: due 10 giây khi bình thường, 2 giây khi abnormal.
- Health: due 60 giây; idle heartbeat due 30 giây khi không có traffic.
- State changes/resync có đường gửi riêng; reconnect gửi attributes/current resync trước khi drain history.
- Đây là best-effort policy, phụ thuộc kết nối, backlog và 4 in-flight slots; không là cam kết thời gian ingest của Cloud.
- RAM events tối đa 32, NVS critical journal 4 slots; QoS1 có thể gửi trùng. Không có bảo đảm exactly-once hay lưu vô hạn.

## Quy tắc dashboard và alarm

1. Validate `schema_version`/`record_kind`; legacy/không hỗ trợ cần route riêng, không default NORMAL.
2. Chỉ dùng current records đủ mới và đúng session cho live state. History là archive.
3. Đọc quality cùng measurements. Key bị bỏ khi invalid/stale không xóa ThingsBoard Latest, nên không dùng latest number cũ làm bằng chứng an toàn.
4. Tách detection, latch, monitoring và Cloud acknowledgment. Offline không tự clear emergency.
5. `recommended_inactivity_ms=90000` chỉ là client attribute khuyến nghị; người quản trị phải cấu hình inactivity phía server.
6. Xác minh ingest/database/Rule Chain trên tenant thực. MQTT CONNECTED/PUBACK không chứng minh các lớp này hoàn thành.

## Artifact và trình tự nghiệm thu

- [Contract chi tiết](../thingsboard/mqtt-data-contract.md): keys, enums, ordering, payloads, dashboard và known discrepancies.
- [CSV dictionary](../thingsboard/PCCC_V1_TELEMETRY_DICTIONARY.csv) và [JSON examples](../thingsboard/PCCC_V1_PAYLOAD_EXAMPLES.json).
- [Schemas và từ điển](../../schemas/pccc-v1/README.md), [fixtures](../../fixtures/pccc-v1/).
- [Routing dry-run Phase 2](../../migration/phase2/README.md): tham chiếu tích hợp, không là Rule Chain production đã triển khai.
- [Checklist Cloud](../thingsboard/cloud-integration-checklist.md): thực hiện trên tenant/device test được cấp quyền, ghi nhận kết quả thực tế.

Nguồn implementation: [tb_mqtt.c](../../main/src/tb_mqtt.c), [cloud_codec.c](../../main/src/cloud_codec.c), [publish policy](../../main/include/publish_policy.h), [clock policy](../../main/include/clock_policy.h).
