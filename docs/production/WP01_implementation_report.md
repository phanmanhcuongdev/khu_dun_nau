# WP01 — Safety state / Physical ACK / Durable emergency latch

Ngày: 2026-11-11. Phạm vi chỉ WP01; D1–D4 đã duyệt. Không triển khai event journal, MQTT schema, publisher budgets hoặc Cloud. Không đổi GPIO, PSRAM, thresholds, credentials, OLED UI hay RGB.

## A. Changed files và baseline

Không có Git metadata khả dụng; không tạo repo. Baseline source/test/config đã copy vào `/tmp/wp01-baseline/`; hashes lưu tại `WP01_baseline_manifest.json`; unified diff tại `WP01_changes.diff`.

| File | Thay đổi |
|---|---|
| `main/src/risk_engine.c`, `main/include/risk_engine.h` | Recovery continuity; async persistent-latch handshake; read-only exports; private physical ACK helper |
| `main/src/latch_store.c`, `main/include/latch_store.h` (new) | NVS record validation/load, bounded worker, generation-correlated SET/CLEAR completion |
| `main/src/touch_btn.c`, `main/include/touch_btn.h` | ANYEDGE tracking, atomic flags, continuous stable hold duration, boot-held inhibit, ISR registration errors |
| `main/src/local_alarm_controller.c` | Hold duration từ driver, independent khỏi edge/mute event; release gọi hold0 re-arm |
| `main/include/sensor_types.h` | Read-only detection_state / ack_eligible / physical_ack_seq / persistence_health |
| `main/main.c` | No automatic NVS erase; conservative boot buzzer; skip network on failed NVS init; persistence enqueue AFTER local alarm update |
| `main/CMakeLists.txt` | Register latch_store.c; dùng nvs_flash dependency đã có |
| `tests/wp01/*` (new) | Recovery regression, real touch driver tests, real store with mocked NVS, engine/controller tests, fake store/stubs, ASan/UBSan runner |
| `tests/oled/run.sh`, `tests/oled/test_ui.c` | Link fake persistence, adapt latch-clear test cho durable async handshake; không đổi OLED renderer/layout |
| `docs/production/WP01_*` | Báo cáo, diff, hashes và kết quả |

Hash đối chiếu: safety_config, board_pins, sensor_pipeline, MLX/MQ2/Flame/ADC acquisition, buzzer source, OLED source/UI, RGB, MQTT, Wi-Fi headers/source và sdkconfig giữ nguyên. MQTT serializer không thêm field WP03. Kitchen/risk struct ABI thay đổi nội bộ, mọi component trong image được rebuild; không dùng struct layout để serialize NVS.

## B. Recovery bug — before / after

`tests/wp01/test_recovery.c` được tạo trước khi sửa. Compile với Risk Engine cũ và OLED host headers: FAIL, exit134 tại assertion đòi vẫn WARNING sau fault (log `WP01_recovery_before.log`).

Sau fix: mọi nhánh INVALID/STALE trong warning/suspicious reset recovery start, và nhánh mất physical_safe/mandatory validity/warmup cũng reset. Full5s an toàn liên tục mới xuống NORMAL. Không đổi detection thresholds/priority hoặc ngưỡng hold.

Test PASS:

- WARNING -> safe2s -> INVALID10s -> safe: vẫn WARNING, sau4999999µs vẫn WARNING, đủ5000000µs NORMAL.
- Cùng trace với STALE.
- Safe bị ngắt bởi85°C (mất recovery-safe nhưng chưa đạt high-temp warning95°C), không dùng timer cũ.
- Concurrent flame +150°C chọn FIRE_DETECTED như trước.
- Historical latch không clear bởi recovery hoặc nhiều tap.

## C. Physical ACK correctness

- ISR bắt cả nhấn và nhả trên GPIO2, lưu timestamp high và reset ngay khi release. Mỗi high phải ổn định30ms trước khi tính hold; đây là debounce đầu vào, không đổi CONFIG_LONG_PRESS_ACK_SEC=3.
- Edge/rising latch phục vụ tap/mute được consume dưới critical section. Nó không làm continuous state thành1 và không cộng thời gian ACK.
- Driver dùng monotonic elapsed từ lần high liên tục cuối, không cộng sample cách quãng. ISR release giữa hai sensor iterations vẫn cắt hold.
- Boot HIGH không armed: phải nhả rồi mới được tính lần hold mới. Hold khi unsafe bị reject; phải nhả/nhấn lại sau khi đủ điều kiện. Sensor invalid không đủ eligibility.
- Kiểm tra gpio_config, ISR service, handler add và interrupt enable; chỉ reuse service IRAM do module đã cài trong lần init trước. Service khác không xác minh được IRAM -> init fail bảo thủ. ISR IRAM dùng gpio_ll_get_level và esp_timer_get_time (cấu hình hiện tại timer in IRAM), không gọi GPIO wrapper nằm trong flash. Init fail -> touch invalid -> không ACK.
- Controller luôn gọi process_touch_hold, kể cả0, để re-arm. Mute vẫn dựa edge/state hiện có và không mute EMERGENCY.
- Không public direct acknowledge helper nữa; production caller duy nhất của process_touch_hold là Local Alarm Controller. Không RPC/Cloud path.

ACK request khác accepted clear: request chỉ được tạo từ hold hợp lệ, cùng evaluation đã xác minh điều kiện phục hồi. Trong lúc chờ NVS, latch và còi vẫn EMERGENCY. physical_ack_seq chỉ được expose tăng sau durable clear được engine chấp nhận; nó được giữ trong record để khôi phục qua reboot. Không tăng trên tap/unsafe request/error. Counter exhaustion hoặc generation exhaustion fail closed.

## D. Persistence design và ordering

### Record / ownership

Namespace NVS `pccc_latch`, key `state`, explicit little-endian24bytes:

| Offset | Value |
|---|---|
| 0 | magic `0x31434350` |
| 4 | version1 |
| 8 | latched0/1 |
| 12 | physical ACK sequence |
| 16 | operation generation |
| 20 | CRC32 of first20bytes |

Không ghi C struct có padding vào NVS. Validate size/magic/version/boolean/CRC. Dùng blob replacement/integrity của NVS, cộng app CRC/version; không tự tuyên bố đã chứng minh tất cả torn-write cases bằng test host.

Safety task là sole owner runtime Risk Engine; NVS worker priority1, stack3072, queue latest1 là sole writer. Critical sections chỉ copy status/generation, không bao quanh flash. Không task network tham gia set/clear.

### SET

1. Detection hiện có xác định EMERGENCY, set runtime latch ngay.
2. Local Alarm bật continuous buzzer.
3. `risk_engine_persistence_step()` được gọi sau Local Alarm, enqueue SET; không I/O NVS trong safety task.
4. Worker nvs_set_blob -> nvs_commit -> read-back+validate. Status DURABLE chỉ khi thành công.

Không enqueue lại mỗi evaluation. Same emergency không ghi mỗi chu kỳ; một pending transition tối đa3 attempts, cách1s khi lỗi. Nếu backend unavailable, giữ latch và FAULT; không có flash attempts ở sensor loop.

### CLEAR

1. Physical hold3s và điều kiện nhiệt/flame hiện có đủ -> ACK request.
2. Engine vòng tiếp re-check eligibility, giữ runtime latch và đợi worker clear generation tương ứng.
3. Worker ghi CLEAR với ACK seq+1, commit/read-back validate.
4. Engine chỉ clear runtime trên completion đúng generation và conditions còn cho phép. Sau đó export seq mới.
5. New FIRE/unsafe/invalid trong lúc clear pending hủy authorization runtime, enqueue SET thế hệ mới; stale queued command bị skip. Request không thành công không tự tắt còi. Một physical hold mới có thể retry clear sau failure.

### Startup / failures

- Xóa nhánh `nvs_flash_erase()` khi init lỗi. Không erase-flash hay NVS toàn bộ.
- Buzzer driver init trước NVS. Record confirmed latched hoặc trạng thái storage không đáng tin -> continuous buzzer trước init mạng/monitoring.
- Record missing (bao gồm lần nâng cấp đầu), corrupt, version unsupported, short/oversized/read error -> **latched=true**, health FAULT; nếu backend hoạt động, enqueue SET để tạo conservative record. Chỉ physical ACK đủ điều kiện mới ghi CLEAR.
- NVS/open/task/queue failure -> conservative latch, explicit log/health; không giả vờ durable. Nếu nvs_flash_init thất bại thì skip Wi-Fi/MQTT init để tránh network ESP_ERROR_CHECK làm abort trước local safety loop. Wi-Fi code/credentials không đổi.
- Storage transient write failure: tối đa3 retries; runtime latch không clear. Sau failure, cần một physical hold mới hoặc service/reboot để retry path tương ứng; không auto-ACK.
- Read-only persistence_health: PENDING0 / DURABLE1 / FAULT2; currently logs/local snapshot only, không publish MQTT.

### Power/reset semantics và giới hạn bắt buộc

Đã commit latch thì reboot đọc lại latch; sensors safe không xóa; record clear chỉ có từ physical authorization, power-cycle không là ACK.

**Có exposure window trước khi SET async commit xong**. CPU reset/mất điện đúng lúc đó có thể chỉ để lại record CLEAR cũ. Tương tự, new hazard xảy ra trong lúc authorized CLEAR đang thực sự ghi flash có thể đi trước SET mới. Generation check chặn stale queued completions/runtime clear, không biến flash async thành transaction atomic với input vật lý đang thay đổi. Không thể hứa persistence cho mọi thời điểm cắt nguồn từ kiến trúc không chặn alarm này. Muốn guarantee cao hơn cần qualification power-cut thực và có thể hold-up/retention hardware hoặc policy storage bổ sung, không giấu giới hạn dưới chữ PASS.

Nếu commit CLEAR đã bền nhưng reset trước lần engine xử lý completion, boot sẽ đọc CLEAR và seq mới: physical authorization đã có trước đó, không xem reset là ACK. Nếu record không thể validate thì restore conservative latch. CRC không là chữ ký chống attacker; eFuse/security provisioning ngoài WP01.

## E. Test matrix / results

Runner: `bash tests/wp01/run.sh`; cc -Wall -Wextra -Werror + ASan/UBSan. LeakSanitizer disabled do môi trường ptrace, không claim leak test.

| Case | Result / evidence |
|---|---|
| Old implementation recovery regression | FAIL expected (exit134), captured before edit |
| INVALID / STALE / interrupted safe / full5s | PASS host actual engine |
| Concurrent fire/temp primary reason | PASS host actual engine |
| Short tap +50 separate pulses | PASS host actual touch ISR/readout; không thành hold |
| Stable debounce, continuous3s, release/repress | PASS host driver/controller |
| Held at boot | PASS host, no hold until release |
| Unsafe/invalid ACK rejection | PASS host engine/controller |
| 5 repeated emergency/ACK cycles | PASS host; seq advances only accepted clear |
| Emergency+safe -> engine re-init remains latched | PASS host fake durable store |
| Physical ACK accepted -> re-init remains cleared | PASS host; hardware restore CLEAR also observed |
| Failed/delayed clear, new hazard cancels pending clear | PASS host deterministic sequencing |
| Real store CRC/version/missing/short/corrupt/read/open failure | PASS mocked NVS with real codec/load code |
| NVS set/commit failure, queue/task allocation failure | PASS host conservative status/latch |
| Stale CLEAR superseded by SET | PASS real store generation check (deterministic host schedule) |
| 100 store SET/CLEAR/reload cycles | PASS host mock flash |
| OLED V2 host integration/layout | PASS; test adjusted for async persistence only |
| RGB host | PASS |
| LD2410 four build/test configurations | PASS |
| Real power-cut/brownout during flash write | NOT TESTED |
| Actual fault-injection corrupt NVS on device | NOT TESTED (không phá NVS để test) |
| Worst-case response<=2s / task resource under load | NOT VERIFIED; không đổi sampling hoặc claim deadline |

Read-only exports:

- detection_state computed from **existing** fire/critical-temperature booleans inside evaluate: EMERGENCY_PRESENT, NO_EMERGENCY khi đủ validity, hoặc UNKNOWN. Không lấy từ reason và không có second classifier.
- ack_eligible lấy từ existing latch recovery checks + storage availability; pending clear không prompt eligibility mới.
- physical_ack_seq đếm successful durable physical latch clear, restore từ record.
- Export trong risk_state_t; MQTT chưa serialize các fields này. OLED/RGB source giữ nguyên.

## F. Build / flash / boot

ESP-IDF6.1 build PASS. Image gần đầy app partition1MiB, còn khoảng3% (~29KiB); không thay partition table trong WP01. Không compiler error; có cảnh báo app partition gần đầy. Flash bản đầu và một bản hoàn thiện ISR cache-safe qua stable port `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90182195-if00` (`ttyACM0`), MAC `e8:f6:0a:8a:27:34`, chip ESP32-S3 rev0.2 đã xác minh; flash hash verified. Không erase-flash/NVS, không fake ACK hoặc inject FIRE bằng software.

Boot capture35s ghi nhận:

```
LATCH_STORE: Restore latch=0 ack_seq=1 health=1
TOUCH_BTN: Continuous physical hold ready; boot-high requires release
RISK_ENGINE: Risk initialized: restored_latch=0 physical_ack_seq=1 persistence=1
```

Người dùng xác nhận đã nhả/giữ nút vật lý và LED trở về xanh lá. Record CLEAR+seq1 được restore sau reset. Lần capture sau đọc CLEAR+seq2; người dùng cũng xác nhận đã thử cảm biến khói/lửa và LED đúng trạng thái. Exact SET/CLEAR transition xảy ra trước cửa sổ serial capture nên không claim đã ghi được timestamp của từng commit.

Reset bằng serial RTS/EN là reset được điều khiển từ host, **không phải HIL power-cut hoặc esp_restart()**. Restore CLEAR qua reset: observed PASS. Restore một record LATCHED=1 trên device qua reset nếu không còn record đang chốt trong lúc capture: NOT TESTED; host restore logic PASS. Không yêu cầu người dùng tạo lửa/khói thật chỉ để hoàn tất test.

Wi-Fi/MQTT và sensor loop tiếp tục hoạt động trong capture; no reset loop/panic/brownout/LEDC/OLED errors thấy trong cửa sổ lưu. Log summary được đính kèm cùng báo cáo, không chứa credentials/payload token. RGB production self-test vẫn disabled.

## G. Unresolved risks

1. Async flash exposure windows mô tả ở D; chưa qualification power-cut. Không bảo đảm latch bền tại mọi nanosecond trước commit.
2. Physical supervision: ISR edge tracking không chứng minh TTP223 không bị stuck/wiring error; boot-held inhibit giúp bảo thủ. Debounce30ms chưa đo waveform trên board, stack3072 NVS worker chưa soak/high-water measurement.
3. NVS không khả dụng vĩnh viễn: alarm stays latched, không ACK-clear đến khi service storage thành công. Không erase tự động để “chữa”.
4. App partition còn3%; WP02 thêm delivery/journal có thể cần release-size review. Không tự bật PSRAM/OTA/đổi partition.
5. Network/event buffer race/loss/serializer/security findings còn nguyên cho WP02–WP07; không lén sửa ngoài phạm vi.
6. UI V2 vẫn dùng existing prompt guards và không hiển thị persistence health chi tiết; log/exports cho thấy lỗi, UI nâng cấp nếu cần là task riêng.

## H. Impact / dependencies for WP02

- Critical event history qua reboot chưa triển khai. Không dùng latch blob như event database.
- Future consumers dùng physical_ack_seq accepted-clear, không transient request/old reason để suy ACK.
- Event generation/order và PUBACK ownership vẫn WP02. Không MQTT queue/outbox budget change ở đây.
- ABI risk_state_t có fields mới; sizeof-based queues rebuild trong cùng image. Flat schema WP03 có thể đọc exports, không tính lại thresholds.
- Mọi future persistence/event writer phải giữ sole-writer ownership hoặc tách namespace rõ; không cloud route gọi clear.

Kết luận: WP01 software implementation và host regression/build/flash hoàn tất; hardware physical ACK/clear restore có bằng chứng và user confirmation. Các phép thử power-cut, restore LATCHED=1 qua reset trên device và latency qualification còn NOT TESTED/NOT VERIFIED như trên; không dùng host PASS thay thế những bằng chứng đó.

Final ISR cache-safety refinement: objdump confirms touch_gpio_isr_handler và esp_timer_impl_get_time nằm .iram0.text. GPIO read trong ISR được inline từ HAL. Physical observations của user ở bản WP01 trước refinement allocation ISR; final image được boot-validated riêng. Không dùng kiểm chứng placement để thay hardware test flash/cache interruption.

Final image SHA256: `2a38d3c8a254df481b1233c2b3974ad39cee3bef14bec7bf549ce46b4eb0b5c6`. Final boot35s: restore latch0/ack_seq2/DURABLE, touch ready, NORMAL_SAFE, MQTT ACKs; no panic/brownout/error entries observed. Evidence `WP01_boot_final.log`, `WP01_test_results.log`, `WP01_after_manifest.json`.
