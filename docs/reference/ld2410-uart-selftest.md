# LD2410C UART self-test — Linux implementation and validation

[← Mục lục tài liệu](../README.md) · Chuẩn hóa tài liệu: **2026-11-12**.

**Trạng thái: tham chiếu kỹ thuật theo phiên kiểm tra.** Pinout và hành vi hiện tại được tóm tắt trong [phần cứng](../guides/hardware.md) và [vận hành](../guides/operation.md). Các log/binary/đường dẫn máy cá nhân bên dưới là provenance cục bộ, không nằm trong bộ tài liệu public.

Historical implementation report. For the subsequent hardware evidence, parser
classification fixes and current diagnostic build, see
[docs/reference/ld2410-uart-investigation.md](ld2410-uart-investigation.md).

## A. Source baseline

Inspected Linux `main/src/ld2410_radar.c`, its header, `board_pins.h`,
`sensor_pipeline.c`, `main.c`, `diagnostic_config.h`, and `main/CMakeLists.txt`.
This directory has no Git metadata; no Git baseline was assumed.

The existing driver was compiled by CMake and invoked by the sensor pipeline.
UART1 already used GPIO18 RX, GPIO17 TX, 256000 baud, 8N1, no flow control.
The GPIO enum/preprocessor problem had already been fixed. Encoder, ACK parser,
and a default-enabled self-test already existed. There was one UART reader.

Remaining defects:

- Only 512 bytes read per 1000 ms safety cycle, with a 1024-byte driver RX ring.
  The link can carry 25600 bytes/s; even a 23-byte measurement at 20 Hz is
  460 bytes/s, leaving little margin. Engineering data at that rate exceeds
  the old read budget. Buffered measurements were returned one per cycle.
- ACK and measurement parsing used separate scans over the same buffer.
  The measurement scan retained only 22 bytes, potentially truncating partial ACKs.
- Measurement lengths were not validated.
- Any NACK redirected to END_CONFIG, including END_CONFIG NACK itself, allowing
  unlimited cleanup commands. An unrelated NACK could also change test state.
- Enable ACK payload validation and tests were missing. TX success accumulated
  with OR, concealing later failed writes.

Before editing, recoverable originals were copied outside the project to
`/tmp/ld2410-backup/main/` (driver, public header, diagnostic config, CMakeLists).
CMakeLists was inspected and backed up but required no change.

## B. Files changed

- `main/src/ld2410_radar.c`: `ld2410_process_stream` unifies framing;
  `ld2410_selftest_send`, `ld2410_selftest_handle_ack`,
  `ld2410_selftest_step`, and reporting validate and bound transactions;
  `ld2410_uart_task` owns UART reads; `ld2410_radar_read` obtains a locked
  latest-measurement snapshot and preserves OUT fallback. UART init failures
  clean up installed resources and retain OUT availability.
- `main/include/diagnostic_config.h`: permits compiler override of the existing
  default-enabled `LD2410_UART_SELFTEST_ENABLED` flag.
- `main/include/ld2410_radar.h`: adds explicit `stddef.h`, documents fallback
  validity, corrects historical OUT comment to actual GPIO8. No pin remapping.
- `tests/ld2410/test_ld2410.c`, `tests/ld2410/stubs/*`, `tests/ld2410/run.sh`:
  Linux host tests compile the actual production driver with hardware doubles.
- This report.

## C. Command encoder

Only these commands and their exact parameter forms can be sent:

```text
Enable: FD FC FB FA 04 00 FF 00 01 00 04 03 02 01
Version: FD FC FB FA 02 00 A0 00 04 03 02 01
End:    FD FC FB FA 02 00 FE 00 04 03 02 01
```

Tests compare every byte. Unsupported commands and excessive/invalid parameters
are rejected before copying. TX logging records expected bytes, accepted bytes,
command and hex; short/error writes mark TX failure. API acceptance does not
establish receipt by radar.

## D. ACK parser and protocol reference

Reference: [Hi-Link LD2410C protocol V1.09, sections 2.1, 2.2.1, 2.2.2,
2.2.8 and 2.3](https://h.hlktech.com/download/HLK-LD2410C-24G/1/LD2410C%20%E4%B8%B2%E5%8F%A3%E9%80%9A%E4%BF%A1%E5%8D%8F%E8%AE%AE%20V1.09.pdf).

ACK framing validates FD FC FB FA, LE16 body length, body bounds 4..64 bytes,
and 04 03 02 01 footer. Only the expected ACK for the current wait state can
advance the test; unrelated ACKs/NACKs are ignored.
Success requires status zero and exact command-specific payload lengths:

| ACK | Body bytes, including command/status | Additional validation |
| --- | --- | --- |
| 01FF | 8 | LE protocol version 1; nonzero buffer size |
| 01A0 | 12 | 8-byte firmware payload: LE16 type, 2-byte main version, LE32 subversion |
| 01FE | 4 | No extra success payload |

A matching NACK or invalid success payload starts cleanup (or terminates a
failed cleanup). Invalid outer framing is counted, resynchronized, and eventually
handled by the finite timeout if no valid matching reply arrives.

The supplied V1.09 firmware example has a structurally valid `0C 00 A0 01`
ACK, but type bytes `00 01` conflict with its documented type `0x0001` under
little-endian encoding. The test covers that exact example and preserves raw
LE type 256, without moving command/status offsets. The corrected type bytes
`01 00` decode to type 1. Both yield V1.02.22062416 from the remaining example
bytes. Actual firmware values are logged; no successful hardware result is
fabricated from these fixtures.

## E. State machine

IDLE → SEND_ENABLE → WAIT_ENABLE → SEND_VERSION → WAIT_VERSION →
SEND_END → WAIT_END → DONE.

Each wait is 750 ms with at most one timeout retry. A timeout after retry on
ENABLE or VERSION enters END cleanup, including the case where ENABLE may
have succeeded but its ACK was lost. END timeout after retry finishes INCOMPLETE;
END NACK finishes without re-entering cleanup. Expected NACK or malformed
success payload skips to cleanup. DONE never transmits again.

At most six commands (each command twice); an all-timeout ENABLE path sends
four commands, and a VERSION-timeout path following successful ENABLE sends
five. The total transaction wait budget is at most 4.5 seconds plus worker
scheduling. No safety-loop delay is introduced. Cleanup cannot guarantee radar
mode when its ACK is missing, so reporting explicitly says INCOMPLETE.

## F. UART runtime compatibility

One FreeRTOS RX task, priority 5 and 4096-byte stack, owns the only
`uart_read_bytes` call. RX ring is 4096 bytes, TX ring 256 bytes, local reads
256 bytes, stream buffer 1024 bytes, snapshot one measurement. Reads wait at
most 10 ms, then the task yields one RTOS tick. Normal stream throughput and
scheduler behavior still require hardware validation under actual system load.

Both frame families are consumed in stream order. Partial frames survive calls,
multiple frames drain in one pass, and ACK-like bytes inside a measurement do
not become commands. Normal 23-byte and engineering 45-byte frames validate
length, type, internal markers, target range and footer; engineering basic
measurement fields retain the same offsets. No engineering-mode command is sent.

Only measurement frames update the snapshot, timestamp or UART telemetry
validity. Snapshot access uses a short cross-core critical section. Older
snapshots are replaced by the latest measurement, intentionally avoiding a
backlog; snapshots older than two seconds are not published as fresh. OUT
remains read on every pipeline call, even after UART/task initialization failure.
ACKs alone produce no radar measurement.

Tag `LD2410_TEST` reports init, start, TX/ACK events, malformed frames, counters,
firmware and one final result table. RX chunk counts use DEBUG level to avoid
continuous stream logging; the final table includes accumulated RX bytes.
The first valid measurement is logged without continuous measurement dumps.
PASS requires UART initialized, successful TX API writes, and all three valid
successful matching ACKs. Normal frames can be NOT TESTED when the report is
printed before the radar resumes streaming; subsequent first-frame logging is
independent evidence.

No changes to board mapping, main loop, sensor pipeline, risk engine, alarm
logic, MQTT schema, credentials, sdkconfig, partitions or I2C implementation.

## G. Tests and build

Actual commands from the project root:

```sh
tests/ld2410/run.sh
source /duong/dan/esp-idf/export.sh
idf.py build
```

Host tests PASS:

- Strict C compilation, `-Wall -Wextra -Werror -fanalyzer`.
- Exact encoding, prohibited command/payload rejection, failed TX.
- Successful transaction and no further sends after completion.
- Every split boundary for ENABLE, VERSION, normal and engineering frames.
- V1.09 firmware example discrepancy, LE distances/version.
- Wrong command, NACK in each wait state, malformed payload/length/footer,
  malformed measurement fields, recovery to valid frames.
- Multiple interleaved measurement/ACK frames and embedded ACK-like payload.
- 100000 deterministic noise bytes with bounded buffer usage and recovery.
- Finite timeout/retry/cleanup paths, initialization failures, OUT high/low,
  ACK-only telemetry exclusion, stale snapshot rejection.
- Self-test disabled: zero transmissions, measurements still parsed.
- AddressSanitizer + UndefinedBehaviorSanitizer: PASS. LeakSanitizer is disabled
  because this execution environment runs under ptrace; ASan/UBSan remain enabled.

ESP-IDF 6.1 / ESP32-S3 build: PASS. No toolchain or dependency installation,
fullclean or cache deletion. A fallthrough warning introduced during the final
IDLE-state refinement was fixed and tests/build repeated. The final build log
is `/tmp/ld2410-build.log`; compiler diagnostics and image size are also
summarized in the chat. Build invokes esptool only for offline image generation,
without a device port.

## H. Hardware verification pending

No flash, monitor or device-node access was performed. After explicit user
authorization, flash the built firmware, collect boot logs, confirm the three
ACK IDs/statuses and actual firmware, then verify valid measurements resume
after END_CONFIG and OUT/presence behavior remains correct. Exercise missing
radar RX/TX responses to observe finite cleanup and truthful FAIL/INCOMPLETE
results. Verify other sensor/network behavior under full runtime load.

## I. Final status

Implementation complete. Host tests validated. Firmware build validated.
Hardware bidirectional UART verification PENDING; no physical UART PASS claimed.
