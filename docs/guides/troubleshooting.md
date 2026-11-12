# Xử lý sự cố

[← Mục lục](../README.md) · Phạm vi: chẩn đoán theo cấu hình và luồng firmware hiện tại.

## Build và cấu hình

| Triệu chứng | Kiểm tra | Hướng xử lý |
|---|---|---|
| Không tìm thấy `idf.py` | Môi trường ESP-IDF chưa kích hoạt | Source `export.sh` từ bản SDK đã cài |
| Lỗi API/component sau đổi SDK | `idf.py --version`, `dependencies.lock`, diff `sdkconfig` | Dùng baseline SDK của dự án; không sửa hàng loạt API chỉ để bỏ lỗi |
| Không tải được dependencies | Mạng, Git và Component Manager | Giữ lockfile; kiểm tra quyền truy cập registry/repository |
| `Invalid private config type`/length | Kiểu JSON và giới hạn UTF-8 | Sửa cấu hình cục bộ theo hướng dẫn, không in token ra log |
| `network disabled` | File cấu hình đang dùng và đủ ba trường | Kiểm tra CMake cached `PCCC_PRIVATE_CONFIG`, build lại |
| OLED host test thiếu header | Dependency SSD1306 chưa tải | Chạy bước configure/build ESP-IDF để tải component trước |

## Kết nối Wi-Fi / MQTT

`Waiting wifi=... trusted_clock=...` giúp phân biệt các bước. Với `wifi=0`, kiểm tra provisioning và mạng. Có IP nhưng `trusted_clock=0` thì kiểm tra DNS, truy cập NTP, epoch của bản build và khả năng clock sync. MQTT bị gate cho tới khi clock trusted.

Nếu TLS/auth thất bại sau khi có clock: đối chiếu broker URI, CA, hostname, ngày chứng chỉ và token thuộc đúng device. Không tắt certificate verification hoặc ghi token vào issue GitHub. Token chỉ xác thực device, không chứng minh quyền admin/tenant.

MQTT CONNECTED nhưng không có dashboard: xác minh topics, ingest và routing trên tenant được cấp quyền. Puback không là bằng chứng database đã lưu. Xem [contract](../thingsboard/mqtt-data-contract.md) và [checklist](../thingsboard/cloud-integration-checklist.md).

## Cảm biến và đầu ra

| Triệu chứng | Điểm cần kiểm tra |
|---|---|
| `FAULT` ngay khi khởi động | GY-906 có valid data không; hoặc flame và radar cùng không hợp lệ |
| `DEGRADED` lúc đầu | MQ-2 warm-up phần mềm 15 s và quality của các cảm biến phụ |
| Radar luôn OUT fallback | TX/RX đấu chéo, UART1 256000 baud, nguồn/GND, frame validity/freshness |
| Không có người nhưng không lên SUSPICIOUS | Bếp phải đang active, radar quality VALID xác nhận vắng đủ 60 s |
| OLED không lên | I2C1 SCL47/SDA21, nguồn, init log; không dùng pinout OLED phiên bản cũ |
| RGB không sáng | Tùy chọn wiring confirmed, R39/G38/B48, loại LED/driver và xung đột board |
| Số đo cũ còn trên ThingsBoard | Key bị omit không xóa Latest; consumer phải kiểm quality và capture age |

## Còi liên tục / không giải tỏa latch

Kiểm tra `risk_level`, `detection_state`, `emergency_latched`, `ack_eligible` và `persistence_health` cùng nhau. Reboot hoặc Cloud ACK không clear latch. Khi nguồn nguy hiểm hết, cần GY-906/flame hợp lệ, nhiệt dưới 110 °C, flame SAFE, storage sẵn sàng và touch hold ít nhất 3 s. Sau đó phải đợi clear được ghi bền.

`persistence_health`: 0=PENDING, 1=DURABLE, 2=FAULT. Không xóa NVS/erase-flash để ép còi tắt; thao tác đó làm mất lịch sử an toàn. Xem [vận hành](operation.md) và log lỗi storage trên máy riêng.

## Thu thập thông tin khi báo lỗi

Ghi commit, phiên bản ESP-IDF, board, thay đổi đấu nối, bước tái hiện và trạng thái risk/quality liên quan. Lấy firmware/build/config identity nếu có. Chỉ chia sẻ đoạn log đã rà soát và ẩn dữ liệu riêng; không đính kèm provisioning, khóa, binary/ELF hoặc production log đầy đủ lên public.

Diagnostics compile-time ở [diagnostic_config.h](../../main/include/diagnostic_config.h) mặc định tắt. Nếu bật UART dumps để chẩn đoán, dùng giới hạn có sẵn, kiểm lại build diff và tắt sau khi xử lý; tham khảo [radar investigation](../reference/ld2410-uart-investigation.md).
