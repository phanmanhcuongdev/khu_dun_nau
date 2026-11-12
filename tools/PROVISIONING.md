# Cấu hình kết nối riêng cho build

[← Mục lục tài liệu](../docs/README.md) · [Hướng dẫn build và cấu hình đầy đủ](../docs/guides/getting-started.md)

Cấu hình Wi-Fi và token thuộc từng thiết bị được giữ trong `private/provisioning.json`, không đưa vào source, fixtures hoặc tài liệu public. Đây là đầu vào build cục bộ, không phải dịch vụ cấp phát cấu hình từ Cloud.

## Tạo cấu hình

Chạy tại thư mục gốc dự án, với file chưa tồn tại:

```bash
mkdir -p private
chmod 700 private
cp tools/provisioning.example.json private/provisioning.json
chmod 600 private/provisioning.json
```

Điền `wifi_ssid`, `wifi_password`, `tb_access_token` bằng trình soạn thảo cục bộ. Không in giá trị trong shell arguments, CI logs hoặc issue. Không copy đè file đã provision.

Sau khi kích hoạt ESP-IDF:

```bash
idf.py build
python3 tools/release_check.py
```

CMake mặc định đọc `private/provisioning.json`. Dùng `-DPCCC_PRIVATE_CONFIG=/duong/dan/tuyet/doi/provisioning.json` nếu cần file khác; đường dẫn được CMake cache. File thiếu/rỗng hoặc chưa đủ trường vẫn build với mạng tắt và local safety bật. Sai kiểu, quá dài hoặc chứa NUL làm configuration thất bại mà không in giá trị.

## Header và artifact

CMake sinh `build/esp-idf/main/private/pccc_build_config.h` ở mode 0600. Giữ file JSON mode 0600, thư mục riêng mode 0700 và giới hạn quyền truy cập build/artifact storage. Wi-Fi dùng RAM storage; firmware không tự xóa các giá trị NVS cũ.

Firmware binary/ELF đã provision chứa thông tin kết nối; không phát hành chúng lên GitHub hoặc CI artifacts public. `.gitignore` giữ private/build/key/log ngoài Git, không ngăn trích xuất credential từ thiết bị có firmware không mã hóa. Release checker chỉ so khớp credential hiện tại trong các nhóm tệp nó duyệt, không thay rà soát bí mật toàn diện.

## TLS và điều khiển

Firmware dùng CA nhúng, kiểm tra hostname và ngày chứng chỉ; MQTT cần đồng bộ SNTP thực trước khi kết nối. Không có bypass cho clock chưa trusted. SNTP không được xác thực mật mã; network/clock bootstrap lỗi thì local safety tiếp tục độc lập. Không có Cloud command subscription, remote ACK hoặc remote threshold control trong triển khai này.

Nguồn: [generator](generate_build_config.py), [CMake](../main/CMakeLists.txt), [ThingsBoard](../docs/guides/thingsboard.md).
