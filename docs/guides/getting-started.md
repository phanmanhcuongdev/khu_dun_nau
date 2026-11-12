# Bắt đầu: môi trường, cấu hình và build

[← Mục lục](../README.md) · Phạm vi: checkout mới, phát triển và nạp firmware.

## Yêu cầu theo repository

| Thành phần | Baseline |
|---|---|
| MCU | ESP32-S3; board tham chiếu N16R8 |
| ESP-IDF | Lockfile hiện ghi `6.1.0`; manifest khai báo tối thiểu rộng hơn nhưng không chứng minh mọi phiên bản đều build được |
| CMake | `CMakeLists.txt` yêu cầu ít nhất 3.22 |
| Python | Python 3 trong môi trường ESP-IDF; host schema tests cần thêm `jsonschema` |
| Kết nối | USB/cổng nối tiếp và quyền truy cập thiết bị; mạng để tải dependencies lần đầu |

Kích hoạt ESP-IDF đã cài trên máy, ví dụ `source <thu-muc-esp-idf>/export.sh` trên Linux. Tài liệu này không cài hoặc thay SDK của máy. Kiểm tra `idf.py --version` trước khi build.

## Lấy mã nguồn

```bash
git clone git@github.com:phanmanhcuongdev/khu_dun_nau.git
cd khu_dun_nau
```

Giữ `dependencies.lock` và `sdkconfig` được theo dõi trong Git. Component Manager tải dependencies vào thư mục bị ignore `managed_components/`; không cần commit bản tải về. Kiểm tra `git diff -- dependencies.lock sdkconfig` sau khi đổi SDK hoặc chạy cấu hình.

Target trong lockfile là `esp32s3`; checkout đã có cấu hình nên không cần chạy `set-target` mỗi lần. Chỉ đổi target sau khi xem lại board, pinout và cấu hình dự án.

## Build không có thông tin kết nối

```bash
idf.py build
```

`tools/generate_build_config.py` được gọi trong bước CMake, sinh `build/esp-idf/main/private/pccc_build_config.h`. Không có file riêng hoặc thiếu một trường bắt buộc thì `PCCC_PROVISIONED=0`: mạng bị tắt, đường giám sát cục bộ vẫn chạy. Cấu hình sai kiểu/độ dài sẽ làm configuration thất bại.

## Cấu hình Wi-Fi và ThingsBoard

```bash
mkdir -p private
chmod 700 private
cp tools/provisioning.example.json private/provisioning.json
chmod 600 private/provisioning.json
```

Nếu đã có file cấu hình, không chạy lại lệnh `cp` đè lên file đó. Điền ba trường bằng trình soạn thảo trên máy:

| Key | Ý nghĩa | Kiểm tra của generator |
|---|---|---|
| `wifi_ssid` | SSID Wi-Fi | Chuỗi, tối đa 32 byte UTF-8 |
| `wifi_password` | Mật khẩu Wi-Fi | Chuỗi, tối đa 63 byte UTF-8 |
| `tb_access_token` | Token của device ThingsBoard | Chuỗi, tối đa 256 byte UTF-8 |

Cả ba phải có giá trị để đánh dấu provisioned. Generator không chấp nhận NUL và file lớn hơn 4096 byte. Không ghi giá trị vào shell arguments, Git hay báo cáo.

Broker hiện cấu hình bằng `TB_BROKER_URI` trong [tb_mqtt.h](../../main/include/tb_mqtt.h); token không tự thay broker. Khi dùng broker khác cần cấu hình URI và CA phù hợp rồi build lại; xem [ThingsBoard](thingsboard.md).

Mặc định CMake đọc `private/provisioning.json`. Có thể chỉ định file khác qua đường dẫn tuyệt đối:

```bash
idf.py -DPCCC_PRIVATE_CONFIG=/duong/dan/tuyet/doi/provisioning.json build
```

`PCCC_PRIVATE_CONFIG` được CMake cache; kiểm tra lại khi chuyển giữa cấu hình test và thiết bị. Giữ cả file ngoài dự án ở quyền riêng tư tương tự. Khi sửa file mặc định, chạy `idf.py build` để tái tạo header.

## Kiểm tra cấu hình board

```bash
idf.py menuconfig
```

Đối chiếu [pinout](hardware.md) trước khi bật RGB. Menu `RGB status` có tùy chọn xác nhận đấu nối và self-test. `sdkconfig` đang theo dõi đã bật xác nhận RGB, dù mặc định Kconfig là tắt. Kiểm tra hoặc tắt tùy chọn này nếu board của bạn chưa được đấu đúng. Code tích hợp chỉ chấp nhận R39/G38/B48 khi RGB được bật; đổi chân cần audit xung đột và sửa phần tích hợp, không chỉ menuconfig.

## Build, flash và theo dõi

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Chọn cổng thực tế: ví dụ `/dev/ttyACM0`, `/dev/ttyUSB0` hoặc `COMx`. Chỉ flash sau khi xác định đúng thiết bị và đấu nối. `Ctrl+]` thoát monitor. Không dùng `erase-flash` như cách xử lý lỗi mặc định: thao tác đó xóa dữ liệu NVS, gồm lịch sử latch/journal.

Khi khởi động, kiểm tra cảm biến, MQ-2 warm-up, OLED và trạng thái latch. Khi đã provision: Wi-Fi phải có IP, clock phải được SNTP đồng bộ và tin cậy rồi MQTT mới kết nối. Kết nối broker không chứng minh Rule Chain/dashboard đã được cấu hình.

## Trước khi đưa thay đổi lên GitHub

```bash
python3 tools/release_check.py
git status --short
git diff --stat
```

Release checker tìm giá trị thông tin kết nối hiện tại trong các nhóm source/tài liệu được nó duyệt; đây không phải bộ phát hiện mọi loại bí mật. Vẫn rà soát diff. Không đưa header riêng, `.bin`, `.elf`, log hoặc khóa riêng lên public.

Nguồn: [CMake](../../main/CMakeLists.txt), [generator](../../tools/generate_build_config.py), [lockfile](../../dependencies.lock), [provisioning](../../tools/PROVISIONING.md).
