# RGB status integration

[← Mục lục tài liệu](../README.md) · Chuẩn hóa tài liệu: **2026-11-12**.

**Trạng thái: tham chiếu kỹ thuật theo phiên kiểm tra.** Pinout và hành vi hiện tại được tóm tắt trong [phần cứng](../guides/hardware.md) và [vận hành](../guides/operation.md). Các log/binary/đường dẫn máy cá nhân bên dưới là provenance cục bộ, không nằm trong bộ tài liệu public.

Kconfig production defaults remain disabled; current hardware test sdkconfig enables
CONFIG_RGB_STATUS_WIRING_CONFIRMED=y; production self-test is disabled.
No GPIO is configured by RGB until explicitly enabled. Do not enable with original wiring.

## Hardware blocker
sdkconfig disables PSRAM; this does not establish availability of pins wired internally
on N16R8. No board schematic or exact module identification was supplied. GPIO35/36
are forbidden by this driver, regardless of PSRAM settings. PSRAM configuration is unchanged.
GPIO40 is already buzzer output. Final requested mapping: R39 / G38 / B48.
From ORIGINAL wiring: move R GPIO38 -> GPIO39, G GPIO36 -> GPIO38,
B GPIO35 -> GPIO48. From previous PROPOSED mapping: R38 -> R39,
G39 -> G38, B41 -> B48. Do not assume either proposal was physically wired.
Keep individual 220 ohm resistors and common cathode at GND.

Board-specific blocker: no schematic/exact board revision is present. GPIO39 is
MTCK (external JTAG); firmware has USB Serial/JTAG support but this does not prove
external JTAG/eFuse routing or physical connections. No eFuse/routing changes made.
GPIO48 drives onboard addressable RGB on Espressif DevKitC-1 v1.0; v1.1 uses GPIO38.
Thus BOTH G38 and B48 require board-specific LED/peripheral connection verification.
No WS2812 driver or runtime GPIO39/38/48 consumer was found outside RGB in this repo.
Some old sensor header comments mention obsolete pins; actual drivers use board_pins.h.
References:
- https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html
- https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html

Keep RGB_STATUS_WIRING_CONFIRMED disabled until board connections and actual rewiring
are confirmed safe. Application static assertion restricts mapping to 39/38/48.

Audit: temperature I2C 5/6; OLED 47/21; flame 4/7; MQ2 11/10;
radar 8/18/17; buzzer 40; touch 2. No servo, camera or LEDC use found in application.
RGB exclusively reserves low speed LEDC timer 3 and channels 5/6/7, 5 kHz, 8 bit.
Future LEDC consumers must honor this reservation; IDF does not provide ownership arbitration.

## Behavior
Consumes risk_engine_step output immediately. EMERGENCY maps FIRE (including engine latch),
then MONITORING_FAULT maps SENSOR_ERROR, WARNING maps WARNING, UNKNOWN maps BOOTING,
and NORMAL/SUSPICIOUS map INFO. No thresholds, buzzer behavior or network error rules change.
Timer task renders every 10 ms, independent of sensor/network tasks. State changes restart
patterns, repeated same-state updates preserve phase. SENSOR_ERROR is 100 ms on,
100 ms off, 100 ms on, 700 ms off. Lifecycle must be serialized and runtime callers
quiesced before deinit (normal task context only). Deinit uses IDF 6.1
esp_timer_stop_blocking before deleting the callback mutex; failed timer allocation
does not deconfigure an unowned LEDC timer. Runtime setters use a mutex; timer callback skips a busy tick.

## Device validation (not performed)
After wiring verification, test raw colors using rgb_status_color(255,0,0), (0,255,0),
(0,0,255), (255,100,0), (160,0,255). Restore state mode afterward.
Exercise each enum: BOOTING/WARNING 500/500 ms, FIRE 160/160 ms, INFO steady.
Check double blink and rapid transitions with logic analyzer; expected timing tolerance
is one 10 ms render interval plus scheduler latency. Inject fault using existing sensor
fault handling and verify simultaneous EMERGENCY remains red. Do not alter fire thresholds.
Verify sensor cadence, MQTT, buzzer and radar remain operational. Exercise off/deinit/init
with no concurrent callers; check all three outputs extinguish and resources can be reused.
No device was flashed and no optical/electrical/servo test was performed.

## Validation commands
python3 tests/rgb_status/run.py
bash tests/ld2410/run.sh
source /duong/dan/esp-idf/export.sh
idf.py build

Only AFTER safety confirmation, rewiring, enabling the Kconfig option and rebuilding:
idf.py -p /dev/ttyACM0 flash monitor
Replace the serial port as appropriate. Exit monitor with Ctrl+].
Hardware tests and actual eFuse/JTAG routing: NOT TESTED.

## Software review and host coverage (2026-11-11)
The callback explicitly uses ESP_TIMER_TASK, not ISR dispatch. LEDC set/update
calls are serialized by the same mutex across callback and setters (IDF marks
these APIs unsafe only when concurrently controlling the same channel).
The timer callback takes the mutex with zero wait and contains no delay or logging.
LEDC channels are deconfigured and timer 3 is paused before timer deconfiguration,
as required by the installed IDF driver. No other application LEDC consumer found.

Host PASS: production pattern calculations, exact application risk presentation
mapping for every existing risk/monitoring combination, FIRE precedence over FAULT,
1000 repeated mapping passes, configuration R39/G38/B48 with enable flag off;
four LD2410 host test runs. Repeated mapping does not test concurrent setters.
NOT TESTED: initialization fault injection, actual concurrent scheduling, LEDC
allocation/reuse on target, eFuse routing, electrical output and peripheral regressions
on hardware. Existing sensor algorithms and buzzer/ThingsBoard source are unchanged.

Full project build: PASS, ESP-IDF 6.1.0, production RGB disabled.
No compiler warning:/error: entries in /tmp/rgb-followup-build.log.
IDF emitted Kconfig/deprecation notes; these are not RGB compiler diagnostics.

## LED-only self-test
CONFIG_RGB_STATUS_SELF_TEST defaults to n and depends on verified wiring.
It runs once from the existing monitoring loop after real risk evaluation:
BOOTING -> INFO -> WARNING -> FIRE -> SENSOR_ERROR, 5 seconds each, total 25 s.
The existing RGB component alone renders PWM; no new task or delay is added.
Real WARNING/FIRE/SENSOR_ERROR cancels testing permanently until reboot.
No risk snapshot is changed and no test signal reaches buzzer or ThingsBoard.
Observe logs `RGB LED-only self-test: ...` and `RGB self-test ended/cancelled`.
Schedule transitions have the existing monitoring-loop cadence; PWM remains on esp_timer.
Host tests verify sequence, completion, cancellation and FIRE precedence in every phase.
Optical output and actual scheduling on ESP32: NOT TESTED.

## Latest device preflight
No ttyACM/ttyUSB device is visible to this execution environment. Sysfs exposes
USB Single Serial VID:PID 1a86:55d3, serial 5B90182195, but no usable serial node;
this does not identify the attached MCU. libusb initialization also failed (-99).
Exact board/revision, onboard RGB data wiring/isolation on GPIO38/48, and external
JTAG connection on GPIO39 still require physical evidence. User confirms external
R39/G38/B48 wiring only. RGB activation, flash and boot monitoring remain blocked.
CONFIG_SPIRAM remains disabled as found; PSRAM boot validation is NOT TESTED.
No eFuse, NVS, routing, partition or PSRAM changes were performed.
Latest software validation: full ESP-IDF 6.1 production build PASS; no compiler
warning/error entries in /tmp/rgb-selftest-build.log. Enabled RGB+self-test branch
also passed ESP32-S3 compiler syntax checking using the existing compile command
with feature defines (no enabled image was flashed). Host RGB/self-test and all
four LD2410 runs PASS. Flash/boot/optical/regression-on-device: NOT EXECUTED.

## User clarification and identification checklist
User confirms physical R39/G38/B48 wiring and no intentionally connected external
JTAG debugger. Board has two USB-C connectors; exact model/revision and onboard
WS2812 connections remain unknown, and no isolation was performed. Do not infer
DevKitC-1 identity from the N16R8 memory suffix or connector count.
Software recheck: target esp32s3, RGB disabled, PSRAM disabled; no other application
GPIO38/39/48 consumer and no other LEDC consumer found. Still no ttyACM/ttyUSB node.
USB bridge descriptors cannot prove board model, RGB wiring or MCU memory variant.
Native USB Serial/JTAG does not itself occupy external MTCK GPIO39 (native USB
uses GPIO19/20); physical routing and eFuse state have not been measured/read.

Required photos, with ALL power disconnected (both USB and external supplies):
1. Entire front and back, perpendicular and sharp enough to read PCB model/revision.
2. Module metal shield markings, including full ESP32-S3-WROOM/... and memory suffix.
3. Close-up of both USB-C sockets, nearby USB/UART labels and USB bridge IC markings.
4. Close-up of every onboard LED and nearby resistor/jumper labels, particularly
   RGB/LED/DIN and anything near header pins 38/48; photograph both PCB sides there.
5. Existing header wiring at 38/39/40/48 so pin labels and wire destinations are visible.
Do not cut traces, remove parts or drive GPIO to identify LED wiring.
If exact board schematic cannot be found, a competent hardware check must identify
onboard LED part/pinout and trace DIN (including series resistors/level shifters)
to GPIO38/48 with power off. A lack of continuity beep alone is not proof of isolation.
A shared DIN is an input load, not automatically output contention; electrical
compatibility, loading and unintended onboard LED behavior still require the actual
circuit. Do not claim a confirmed conflict solely from the presence of an RGB LED.
Keep activation/flash blocked until this board-specific assessment is complete.

## Activation authorization (latest; supersedes earlier board-photo blocker)
User confirms external R39/G38/B48 wiring and accepts unintended onboard LED activity
if sharing GPIO. No concrete conflicting GPIO output was found in the application.
Do not require board revision/photos as an activation condition in this session.
Native host access identifies /dev/ttyACM0 via stable path
/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90182195-if00.
esptool confirms ESP32-S3 rev v0.2, MAC e8:f6:0a:8a:27:34,
embedded PSRAM 8MB and external flash 16MB. Sandbox hides the serial device;
read-only identity commands succeeded outside sandbox. PSRAM config is unchanged.
Self-test remains cancelled by any real warning/fault/fire; this can legitimately
prevent the full visual test sequence when sensors report a real fault.

## Actual deployment result (2026-11-11)
RGB enabled: yes. Self-test enabled for this test firmware (Kconfig default remains n).
Full ESP-IDF 6.1 build PASS; host RGB/self-test and four LD2410 tests PASS.
Flashed once via stable USB serial path above; esptool verified written hashes.
No erase-flash, NVS clear, credential, partition layout, PSRAM, eFuse or routing changes.
45-second serial capture after one monitor reset: boot and RGB initialization PASS,
no LEDC errors, panic, brownout or repeated reset markers observed in this window.
Evidence: validation/rgb_boot_summary.log (filtered); full temporary capture:
/tmp/rgb-device-boot.log; flash log /tmp/rgb-device-flash.log.

Self-test commands observed: BOOTING 2.934s, INFO 8.104s, WARNING 13.264s,
FIRE 18.624s. At 21.294s real MQ2 warning GAS_SMOKE_ELEVATED cancelled the
self-test and returned LED control to Risk Engine. SENSOR_ERROR phase NOT TESTED
on device. No optical colors, brightness or timing measurements were obtained.
Sensors continue producing readings; Wi-Fi acquired IP, MQTT connected and broker
ACKs observed. Buzzer telemetry becomes active for actual sensor warning.
Later Flame DO asserted low, producing EMERGENCY/FIRE_DETECTED and then
EMERGENCY_LATCHED. This is actual sensor input, not self-test injection; no latch
acknowledgement or reset was performed to clear it. User must inspect physical
conditions/sensors. PSRAM initialization NOT TESTED (existing config disabled).
The firmware remains the enabled self-test build; real alarms permanently cancel
self-test for the current boot. Do not report physical LED output PASS from logs.

## Production configuration (latest, supersedes test-build settings above)
User confirmed observed RGB colors matched the deliberately exercised states.
CONFIG_RGB_STATUS_WIRING_CONFIRMED=y; R39/G38/B48 unchanged.
CONFIG_RGB_STATUS_SELF_TEST is not set. No component, risk mapping, sensor,
buzzer, physical button, or EMERGENCY_LATCHED implementation changes.

Production deployment: ESP-IDF 6.1 build PASS; flash PASS (written hash verified),
ESP32-S3 MAC e8:f6:0a:8a:27:34 via /dev/ttyACM0 stable by-id path.
45-second boot capture confirms RGB init R39/G38/B48 and no self-test messages;
image contains RGB init but no self-test log strings. No reset loop/brownout seen.
Risk NORMAL/NORMAL_SAFE, Wi-Fi and MQTT broker ACKs observed.
Evidence: validation/rgb_production_boot_summary.log. No runtime alarm latch
was acknowledged/cleared by software; firmware flash/reset restarts runtime state
according to the existing implementation. EMERGENCY_LATCHED source unchanged.
