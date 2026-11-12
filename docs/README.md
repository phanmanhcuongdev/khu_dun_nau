# Tài liệu dự án

Cập nhật: **2026-11-12**. Bộ hướng dẫn này mô tả mã nguồn đang được lưu trong repository; các tài liệu review và báo cáo triển khai giữ bối cảnh của thời điểm viết.

## Lộ trình đọc

1. [README dự án](../README.md): mục tiêu, khả năng và cách build nhanh.
2. [Bắt đầu](guides/getting-started.md): môi trường, provisioning, build, flash.
3. [Phần cứng](guides/hardware.md): GPIO, nguồn tín hiệu, I2C/UART/ADC.
4. [Kiến trúc](guides/architecture.md): tác vụ, module và luồng dữ liệu.
5. [Vận hành](guides/operation.md): trạng thái, ngưỡng, còi, mute, giải tỏa latch.
6. [ThingsBoard](guides/thingsboard.md): transport, payload và routing.
7. [Kiểm thử](guides/testing.md) và [xử lý sự cố](guides/troubleshooting.md).

## Tài liệu theo nhóm

| Nhóm | Tài liệu | Vai trò |
|---|---|---|
| Hướng dẫn | `guides/` ở trên | Điểm bắt đầu cho người sử dụng và phát triển |
| MQTT V1 | [Contract handoff](thingsboard/mqtt-data-contract.md) | Chi tiết keys, enum, timestamp, ordering, dashboard và giới hạn |
| Nghiệm thu Cloud | [Checklist](thingsboard/cloud-integration-checklist.md) | Công việc cần thực hiện trên tenant được cấp quyền |
| Dữ liệu Cloud | [CSV dictionary](thingsboard/PCCC_V1_TELEMETRY_DICTIONARY.csv), [JSON examples](thingsboard/PCCC_V1_PAYLOAD_EXAMPLES.json) | Artifact có cấu trúc; giữ tên để tương thích tài liệu/công cụ |
| Sơ đồ phần cứng | [PNG breadboard](hardware/so_do_khu_dun_nau_bb.png), [thiết kế Fritzing](hardware/so_do_khu_dun_nau.fzz) | Bản thiết kế được cung cấp, xem cùng bảng GPIO |
| Schema | [Contract và dictionary](../schemas/pccc-v1/README.md), [telemetry](../schemas/pccc-v1/telemetry.schema.json), [attributes](../schemas/pccc-v1/attributes.schema.json) | Kiểm tra cấu trúc payload bằng máy |
| Routing | [Phase 2 handoff](../migration/phase2/README.md) | Tham chiếu dry-run, chưa là cấu hình tenant production |
| OLED | [UI V2](reference/oled-ui-v2.md) | Bố cục, ưu tiên hiển thị và giới hạn màn hình |
| RGB | [RGB status](reference/rgb-status.md) | Mẫu màu, chân và tự kiểm tra LED |
| Radar | [UART investigation](reference/ld2410-uart-investigation.md), [UART self-test](reference/ld2410-uart-selftest.md) | Ghi chép kỹ thuật và chẩn đoán có bối cảnh |
| Triển khai | [WP01](production/wp01-implementation-report.md), [Phase 1](production/phase1-implementation-report.md) | Báo cáo lịch sử về latch và network/contract |
| Review | [Readiness](reviews/production-readiness-review.md), [kế hoạch](reviews/production-implementation-plan.md), [đề xuất dữ liệu](reviews/thingsboard-data-model-proposal.md) | Audit/đề xuất trước triển khai; không dùng như đặc tả hiện tại |

## Quy ước và nguồn sự thật

- Mỗi hướng dẫn có phạm vi, liên kết nguồn và đường quay về mục lục.
- Tài liệu viết tiếng Việt; tên enum, key và API giữ nguyên theo code.
- Đường dẫn liên kết tương đối để đọc được trên GitHub và checkout khác.
- Ngưỡng/GPIO lấy từ headers; không suy hành vi hiện tại từ proposal cũ.
- Schema và serializer quyết định wire format. Fixtures minh họa dữ liệu, không là bằng chứng đo phần cứng.
- Báo cáo ghi PASS là kết quả đã báo cáo tại thời điểm đó. Một lần sửa tài liệu không tự tái xác minh build/flash/Cloud.
- Logs, manifests, patches và thư mục evidence production không phát hành trong repository public. Các nhắc đến chúng trong báo cáo lịch sử chỉ là provenance, không hứa có tệp để tải.

Khi sửa firmware, cập nhật hướng dẫn tương ứng; nếu đổi wire contract, cập nhật schema, fixtures, dictionary và kiểm thử cùng thay đổi đó. Không thêm mật khẩu, token, khóa riêng hoặc bản firmware đã provision vào ví dụ.
