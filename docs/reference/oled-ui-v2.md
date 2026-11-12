# OLED UI V2

[← Mục lục tài liệu](../README.md) · Chuẩn hóa tài liệu: **2026-11-12**.

**Trạng thái: tham chiếu kỹ thuật theo phiên kiểm tra.** Pinout và hành vi hiện tại được tóm tắt trong [phần cứng](../guides/hardware.md) và [vận hành](../guides/operation.md). Các log/binary/đường dẫn máy cá nhân bên dưới là provenance cục bộ, không nằm trong bộ tài liệu public.

Presentation only. SSD1306 128x64 0x3C, SDA21/SCL47 unchanged.
Snapshot -> pure oled_ui_compose -> oled_ui_render -> changed-page SSD1306 writes.
Existing 8x8 font reused: title stretched vertically 2x, temperature scaled 2x2.
Header y0 (8px), main y16 (16px), detail y36, action y46, footer y56.
No scrolling, clear-screen flashes or added font atlas. Renderer rejects bounds
violations rather than clipping. Durations above 999s explicitly show >999s.

OLED I2C is synchronous and can time out: one low-priority worker (4096-byte stack)
is necessary to isolate it from safety processing. Queue length 1, overwrite with
latest snapshot, no wait on sensor/Local Alarm caller. Worker is sole OLED owner.
Refresh on input or 1s timeout for freshness/heartbeat; only changed pages sent.
Heartbeat follows monotonic time; input cadence remains the existing sensor loop.
OLED API has no transfer result; successful init/log is not optical verification.
An I2C transfer error is logged by existing driver; previous-page cache cannot
prove actual display RAM content after a failed bus transfer. No bus recovery added.

## State mapping (existing state/flags only)
| Input | UI |
|---|---|
| Initial boot / UNKNOWN without fault | DANG KHOI DONG, CHO MQ2 Ns if warmup |
| NORMAL | Object temperature, NHIET DO VAT THE, DANG GIAM SAT |
| NORMAL + DEGRADED | Same temperature, GIAM SAT HAN CHE |
| SUSPICIOUS | DANG THEO DOI + engine's primary reason |
| WARNING | CANH BAO + primary reason, KIEM TRA KHU VUC |
| EMERGENCY / latch not eligible for ACK | NGUY HIEM + primary reason, ROI XA KHU VUC, GOI HO TRO |
| Latched and engine reason LATCHED + recovery prerequisites valid | BAO DONG DA CHOT, GIU NUT XAC NHAN, GIU 3s TAI CHO |
| FAULT or sensor error/stale reason (below WARNING) | LOI CAM BIEN, temperature/flame sensor if identifiable |

EMERGENCY takes precedence over fault/warmup. WARNING remains visible even with
sensor error. No recomputed danger classification. ACK prompt guards mirror the
engine's existing valid temperature < TEMP_EMERGENCY_RECOVER and valid safe flame,
plus temperature freshness. This is prompt eligibility only: no ACK is issued.
This also handles engine retaining LATCHED reason while invalid sensors prevent ACK.
Recovery countdown and existing mute flag are shown when applicable.
Network footer W:OK M:OK is 9 characters; -- indicates disconnected.

## Complete reason mapping
| Reason | Message |
|---|---|
| NONE | DANG THEO DOI |
| INITIALIZING | CHO CAM BIEN |
| FIRE_DETECTED | PHAT HIEN LUA |
| CRITICAL_TEMPERATURE | NHIET DO RAT CAO |
| HIGH_TEMPERATURE | NHIET DO CAO |
| RAPID_TEMPERATURE_RISE | NHIET TANG NHANH |
| UNATTENDED_COOKING | BEP VANG NGUOI |
| GAS_SMOKE_ELEVATED | KHI GAS/KHOI CAO |
| SENSOR_FAULT | LOI CAM BIEN |
| SENSOR_STALE | DU LIEU QUA HAN |
| RECOVERY_PENDING | CHO PHUC HOI |
| EMERGENCY_LATCHED | BAO DONG DA CHOT |
| EMERGENCY_ACKNOWLEDGED | DA XAC NHAN |
| NORMAL_SAFE | DANG GIAM SAT |
| Unknown future enum | KIEM TRA HE THONG |

No missing current enums. A dedicated state's message can replace the detail
(e.g. fault identifies temperature sensor, warmup gives countdown).
Only engine-selected primary reason is shown; no independent multiple-cause ranking.

Temperature is temp_object_filtered, not ambient. Only gy906_valid + QUALITY_VALID
and update age <= existing GY906_STALE_TIMEOUT_MS, finite value are displayed.
Unavailable/stale/out-of-format numbers use --.-C; no old value is shown as current.
No detection threshold, telemetry, sensor acquisition or GPIO is changed.

## Validation
Run bash tests/oled/run.sh. Production composer/renderer compiled with ASan/UBSan;
LeakSanitizer disabled because environment ptrace prevents it (no heap in renderer).
Covers all risk/reason/monitoring/network combinations, large countdowns, bounds
canaries, unsupported width rejection, stale/NaN values and immutable snapshots.
Tests link the actual unchanged Risk Engine for NORMAL -> FIRE -> LATCHED ->
invalid sensor (refuse ACK prompt) -> valid -> physical hold API -> NORMAL.
No device alarm is injected by these host tests.
Host RGB and LD2410 regression tests rerun. ESP-IDF 6.1 build PASS.
Preview: validation/oled_ui_v2_mockup.png, generated from production renderer PBMs.

NOT TESTED: actual physical button/latch sequence with this UI, fault/disconnect
injection on device, I2C failure recovery, worst-case timing under load, optical
legibility/orientation. These require observation/measurement; mockup is not a photo.

## Device deployment
Flash PASS: ESP32-S3 rev0.2 MAC e8:f6:0a:8a:27:34 via
/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90182195-if00 (ttyACM0).
Hash verified. 45-second boot capture: SSD1306 configured, Adaptive UI V2 ready
at 584ms, sensors running, NORMAL_SAFE, Wi-Fi IP and MQTT broker ACKs observed.
No I2C/layout error, panic, stack overflow or repeated reset seen during capture.
Evidence: validation/oled_v2_boot_summary.log. Full temporary log /tmp/oled-v2-boot.log.
Optical verification still required. Existing RGB self-test remains disabled.
Only presentation files, main CMake registration, tests and docs/artifacts changed.
SHA256 confirms safety modules, RGB source and sdkconfig unchanged. No Git metadata
is present; pre-edit copies were used for comparison.

Later in the same boot capture, real sensor input produced WARNING, then
RECOVERY_PENDING (still WARNING at 43s). No test event was injected or alarm
overridden. This observation is not optical verification of the warning screen.
