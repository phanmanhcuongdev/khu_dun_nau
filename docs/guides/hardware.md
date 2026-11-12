# Phần cứng và đấu nối

[← Mục lục](../README.md) · Phạm vi: cấu hình chân hiện tại trong firmware ESP32-S3.

## Board và thiết bị

Board tham chiếu là ESP32-S3 N16R8. Danh sách bên dưới mô tả tín hiệu firmware sử dụng; điện áp cấp nguồn và mạch chuyển mức cần đối chiếu đúng phiên bản module thực tế. Không suy điện áp an toàn của chân ESP32 từ điện áp cấp module; giữ chung GND và kiểm tra mạch chia áp/chuyển mức cho tín hiệu vượt miền điện áp của MCU, đặc biệt AO của module MQ-2/flame.

| Thiết bị | Tín hiệu module → ESP32-S3 | Cấu hình |
|---|---|---|
| GY-906 / MLX90614 | SCL → GPIO5, SDA → GPIO6 | I2C0, 100 kHz, địa chỉ `0x5A` |
| OLED SSD1306 | SCL → GPIO47, SDA → GPIO21 | I2C1 riêng, 100 kHz |
| Flame | DO → GPIO4 | Active-low: LOW được diễn giải là phát hiện lửa |
| Flame | AO → GPIO7 | ADC1 channel 6, `FLAME_USE_AO=1` |
| MQ-2 | DO → GPIO11 | Active-low: LOW được diễn giải là khí/khói vượt ngưỡng module |
| MQ-2 | AO → GPIO10 | ADC1 channel 9, `MQ2_USE_AO=1` |
| LD2410 | OUT → GPIO8 | Đầu ra hiện diện số/fallback |
| LD2410 | TX → GPIO18 (ESP RX) | UART1, 256000 baud |
| LD2410 | RX ← GPIO17 (ESP TX) | UART1, TX/RX đấu chéo |
| Buzzer qua BC337 | Điều khiển ← GPIO40 | Active-high |
| TTP223/TMP223 | OUT → GPIO2 | Active-high |
| RGB tùy chọn | R ← GPIO39, G ← GPIO38, B ← GPIO48 | Chỉ bật sau xác nhận đấu nối trong Kconfig |

Nguồn chân: [board_pins.h](../../main/include/board_pins.h). RGB lấy từ [Kconfig](../../components/rgb_status/Kconfig) và kiểm tra tích hợp trong [main.c](../../main/main.c).

## Bus và xung đột chân

GY-906 và OLED dùng hai I2C controller riêng. Không nối hai cặp SDA/SCL thành một bus theo sơ đồ của phiên bản khác. OLED hiện dùng SCL GPIO47; chú thích code nêu đã chuyển khỏi GPIO20 để tránh USB D+.

GPIO39 có thể xung đột external JTAG; GPIO38/48 cần kiểm tra LED/peripheral tích hợp của đúng board. `RGB_STATUS_WIRING_CONFIRMED` không thay thế kiểm tra dây thực tế. Kconfig self-test chỉ hiển thị LED, không tạo tình huống nguy hiểm và không đổi risk/buzzer.

## Cách hiểu dữ liệu cảm biến

- GY-906 cung cấp nhiệt độ vật thể và nhiệt độ môi trường tại cảm biến. Giá trị vật thể phụ thuộc vị trí và mục tiêu đo; không coi hai đại lượng là một.
- MQ-2 DO là kết quả comparator của module. AO là ADC thật tại MCU; `mq2_adc_counts` không phải ppm. Có calibration mới phát `mq2_adc_mv`; đó là mV ở chân ADC, không phải điện áp cấp cảm biến.
- Flame AO được đọc theo cấu hình, nhưng không được suy mọi giá trị AO đều có key telemetry tương ứng; wire contract chỉ do serializer quyết định.
- Radar ưu tiên frame UART hợp lệ và có đường OUT fallback. `OUT_FALLBACK` không tự chứng minh chất lượng hiện diện là hợp lệ; phải đọc cả quality và freshness.
- `QUALITY_VALID` chỉ nói đến việc phần mềm thu nhận/kiểm tra dữ liệu. Với chân DO/OUT, mức logic không chứng minh dây hay module còn kết nối vật lý.

## Trình tự kiểm tra trên bàn

1. Ngắt nguồn, đối chiếu pinout, GND, hướng TX/RX và điện áp tín hiệu.
2. Kiểm tra hai bus I2C, nguồn MQ-2 và đường điều khiển transistor còi.
3. Flash firmware, quan sát log khởi tạo; kiểm tra GY-906 có mẫu hợp lệ.
4. Chờ MQ-2 hết warm-up phần mềm; xác minh module thực tế đã ổn định theo điều kiện thử nghiệm.
5. Kiểm tra radar UART và OUT; không đổi trạng thái radar lỗi thành kết luận vắng người.
6. Kiểm tra OLED, nút và RGB bằng các hướng dẫn tham chiếu. Không tạo lửa/khí thật để kiểm tra chức năng phần mềm.

Các ghi chép chẩn đoán chi tiết: [radar UART](../reference/ld2410-uart-investigation.md), [self-test](../reference/ld2410-uart-selftest.md), [OLED](../reference/oled-ui-v2.md), [RGB](../reference/rgb-status.md).
