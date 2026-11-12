# Vận hành: trạng thái, ngưỡng và cảnh báo

[← Mục lục](../README.md) · Phạm vi: logic hiện có trong Risk Engine và Local Alarm Controller.

## Các trạng thái cần đọc riêng

`risk_level` là mức nguy cơ; `monitoring_status` là khả năng giám sát; `detection_state` mô tả bằng chứng khẩn cấp hiện tại. `emergency_latched` là chốt cảnh báo đã lưu. Ví dụ, lửa đã hết nhưng latch còn true thì nguy cơ vẫn là EMERGENCY cho tới khi giải tỏa tại chỗ thành công.

| Risk | Diễn giải và đầu ra |
|---|---|
| `UNKNOWN` | Chưa đủ điều kiện kết luận; không hiển thị như an toàn |
| `NORMAL` | Engine hiện kết luận không có luật nguy cơ đang giữ |
| `SUSPICIOUS` | Bếp hoạt động, radar hợp lệ xác nhận vắng người đủ thời gian; không bật còi alarm |
| `WARNING` | Quá nhiệt, tăng nhiệt kèm khí hoặc khí/khói duy trì; còi ngắt quãng nếu chưa mute |
| `EMERGENCY` | Lửa/nhiệt khẩn cấp hoặc latch vẫn giữ; còi liên tục, hủy mute |

Monitoring có `OK`, `DEGRADED`, `FAULT`. GY-906 không hợp lệ gây FAULT; cùng mất flame và radar cũng gây FAULT. Thiếu cảm biến phụ hoặc MQ-2 warm-up có thể gây DEGRADED. Không đồng nhất Wi-Fi offline với sensor fault.

## Ngưỡng hiện tại

Các giá trị dưới đây lấy từ [safety_config.h](../../main/include/safety_config.h). Chúng là cấu hình dự án, không là ngưỡng chứng nhận cho mọi bếp hay cảm biến.

| Tham số | Giá trị | Vai trò |
|---|---|---|
| Nhiệt bếp hoạt động | 55 °C | Một điều kiện nhận biết đang nấu |
| Tăng nhiệt tối thiểu | 0,5 °C/s | Một điều kiện khác nhận biết hoạt động khi dT/dt hợp lệ |
| Nhiệt WARNING / phục hồi | 95 / 80 °C | Ngưỡng lên WARNING và hysteresis về an toàn |
| Nhiệt EMERGENCY / đủ điều kiện hạ nhiệt | 140 / 110 °C | Ngưỡng khẩn cấp; phục hồi cần nhỏ hơn 110 °C và các điều kiện khác |
| Tăng nhiệt cảnh báo | 2,5 °C/s | Phải kết hợp chỉ báo khí theo luật engine |
| Vắng người | 60 s | Bếp hoạt động + radar hợp lệ xác nhận vắng người |
| Giữ khí/khói | 3 s | Áp dụng luật gas leak; không phải delay chung cho mọi WARNING |
| Xác nhận phục hồi | 5 s | Luật phục hồi WARNING/SUSPICIOUS khi đủ điều kiện liên tục |
| MQ-2 ADC cảnh báo | 800 counts | So sánh giá trị raw filtered, không phải ppm |
| MQ-2 warm-up phần mềm | 15 s | Khoảng khởi tạo của firmware, không phải chứng minh hiệu chuẩn module |
| Nhấn giữ physical ACK | 3 s | Chỉ có hiệu lực khi engine cho phép |
| Mute | 60 s | Tạm tắt còi WARNING/FAULT |
| Còi WARNING | Bật 300 ms / tắt 400 ms | Pattern điều khiển |
| Còi FAULT | Bíp 100 ms / chu kỳ 5000 ms | Nhắc lỗi khi nhánh alarm tương ứng được chọn |

Luật lửa và nhiệt khẩn cấp được ưu tiên, có thể nhảy thẳng tới EMERGENCY. Nhánh high-temperature WARNING không đợi hold gas 3 giây. Dữ liệu lỗi/stale có thể giữ mức nguy cơ và dừng bộ đếm phục hồi; xem [risk_engine.c](../../main/src/risk_engine.c) nếu điều chỉnh logic.

## Mute bằng nút cảm ứng

Khi WARNING hoặc monitoring FAULT, thao tác chạm ngắn có thể bật mute 60 giây. Mute chỉ ảnh hưởng còi; không xóa risk, latch hay telemetry. Nếu EMERGENCY xuất hiện, mute bị hủy ngay và còi chuyển liên tục. Khi hết thời gian, còi quay lại nếu điều kiện cảnh báo còn giữ.

## Giải tỏa emergency latch

1. Xử lý nguyên nhân nguy hiểm thực tế, không chỉ tắt/khởi động lại nguồn.
2. Chờ GY-906 và flame hợp lệ; nhiệt vật thể phải dưới 110 °C, flame phải SAFE và latch store phải sẵn sàng. Kiểm tra `ack_eligible`.
3. Giữ nút cảm ứng ít nhất 3 giây bằng tín hiệu touch hợp lệ.
4. Chờ yêu cầu clear được ghi bền vào NVS; trước khi commit được xác nhận, latch/còi vẫn giữ.
5. Xác nhận `emergency_latched=false` và `physical_ack_seq` tăng so với baseline đã biết; theo dõi risk/monitoring hiện tại.

`ack_eligible=true` không nghĩa là ACK đã nhận. Việc clear latch không chứng minh mọi sensor khỏe hoặc không còn WARNING. Cloud Alarm ACK không gửi lệnh physical clear; firmware hiện không subscribe RPC/remote ACK. Reboot không là thao tác xác nhận.

## OLED và RGB

OLED ưu tiên trạng thái khẩn cấp/latch, lỗi và cảnh báo; chi tiết bố cục ở [OLED UI V2](../reference/oled-ui-v2.md). Thông tin Wi-Fi/MQTT trên UI chỉ phản ánh kết nối.

Nếu RGB đã bật và đấu đúng, mapping trong main ưu tiên EMERGENCY → FIRE, sau đó monitoring FAULT → SENSOR_ERROR, WARNING → WARNING, UNKNOWN → BOOTING, còn lại INFO. Vì vậy INFO không đủ để suy riêng risk NORMAL: SUSPICIOUS cũng đi nhánh này. Xem [RGB reference](../reference/rgb-status.md) để biết pattern cụ thể.

Nguồn: [risk engine](../../main/src/risk_engine.c), [alarm controller](../../main/src/local_alarm_controller.c), [latch store](../../main/src/latch_store.c).
