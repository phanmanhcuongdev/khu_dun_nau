# Kiểm thử và kiểm tra trước phát hành

[← Mục lục](../README.md) · Phạm vi: chạy kiểm tra có sẵn; phân biệt host, build và thiết bị.

## Môi trường host

Cần Bash, Python 3, compiler C (`cc`, GCC có `-fanalyzer` cho radar suite), AddressSanitizer/UndefinedBehaviorSanitizer và `jsonschema` cho schema validation. Các lệnh bên dưới chạy tại thư mục gốc repository.

Nếu chưa có `jsonschema`, tạo môi trường riêng:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install jsonschema
```

Môi trường này dành cho host tests; build firmware dùng môi trường Python của ESP-IDF. Không commit `.venv/`.

## Ma trận kiểm thử

| Lệnh | Kiểm tra | Giới hạn |
|---|---|---|
| `bash tests/wp01/run.sh` | Recovery, latch store, touch và local alarm state | Fake/stub phần cứng; không thay power-cut test |
| `bash tests/phase1/run.sh` | Queue, delivery, journal, codec, policy, Wi-Fi và network scenarios | Host fault injection, không phải RF/TCP outage thật |
| `python3 tests/phase1/validate_contract.py` | Schema Draft 2020-12, fixtures, enum/semantics và negative cases | Không xác minh ingest trên tenant |
| `python3 tests/phase1/test_migration.py` | Routing dry-run, dedup, order, heartbeat, legacy quarantine | Không ghi Cloud/Rule Chain |
| `bash tests/ld2410/run.sh` | Parser radar, diagnostic/self-test variants, sanitizer | Không chứng minh dây/module thực |
| `bash tests/oled/run.sh` | OLED framebuffer/UI và mapping risk liên quan | Cần dependency SSD1306 đã tải; không kiểm tra màn hình vật lý |
| `python3 tests/rgb_status/run.py` | Pattern RGB, mapping và logic self-test | Không phủ LEDC hoặc độ sáng thực tế |
| `python3 tools/release_check.py` | Không có credential hiện tại trong nhóm public inputs được duyệt | Không là secret scanner toàn diện |

Các shell suites sinh binary và output trong `/tmp/` qua `mktemp`, không cần commit output. Một số dùng `ASAN_OPTIONS=detect_leaks=0` vì môi trường sandbox; ASan/UBSan vẫn chạy theo script. Không coi sanitizer không chạy được là PASS.

## Kiểm tra fixture sinh bởi serializer

`tests/phase1/run.sh` in thư mục kết quả, trong đó có `fixtures/`. Dùng đường dẫn thực tế nó in ra:

```bash
python3 tests/phase1/validate_contract.py /tmp/pccc-phase1-tests.XXXXXX/fixtures
```

Thay `XXXXXX` bằng thư mục thật. Việc validate fixture checked-in và fixture mới sinh giúp phân biệt contract hiện tại với ví dụ lưu trong repo. Không chạy generator để cập nhật artifact một cách mù quáng; review diff schema/dictionary/fixtures khi thay serializer.

## Build firmware

```bash
idf.py build
idf.py size
```

Build thành công chỉ xác minh toolchain/config/link của cấu hình đó. Không flash như một phần của kiểm tra tài liệu. Build/flash report trong `docs/production/` ghi lại kết quả tại thời điểm báo cáo; cần chạy lại nếu thay code, board, dependencies hoặc cấu hình.

## Nghiệm thu phần cứng và mạng

Trên thiết bị test đã xác định đúng, kiểm tra init sensors, quality/freshness, warm-up, lỗi thiếu cảm biến, đầu ra còi/OLED/RGB, nút, latch qua reboot và physical clear. Các ca mất nguồn khi ghi NVS, outage thật và long soak phải ghi nhận riêng, không suy từ host tests. Chỉ dùng mô phỏng tín hiệu hoặc môi trường kiểm thử được kiểm soát.

Kiểm tra Cloud theo [checklist](../thingsboard/cloud-integration-checklist.md). Không tự gửi fixtures EMERGENCY lên device production hoặc dùng alarm ACK để thử xóa latch.

## Phát hành lên public

Rà soát `git diff`, chạy release checker và kiểm tra `git status`. Giữ cấu hình riêng, khóa, firmware đã provision, logs và production evidence ở ngoài Git. Ghi rõ lệnh đã chạy, kết quả, SDK/config, host hay thiết bị và phần chưa kiểm chứng. Không đánh dấu các checkbox live test nếu chỉ đọc code.
