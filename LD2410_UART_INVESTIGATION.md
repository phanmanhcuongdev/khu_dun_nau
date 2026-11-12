# LD2410C UART echo / ACK investigation — 2026-11-09

## A. Observed TX/RX evidence

**RAW_RX_CAPTURE_NOT_AVAILABLE**. The old monitor log has TX hex, but no raw RX
hex dump. The old driver logged RX chunk counts at DEBUG and framing metadata
at INFO. `RADAR_UART_HEX_DUMP_ENABLED` did not enable any capture in that driver.
Do not label the physical test `EXACT_TX_ECHO_OBSERVED` from these logs alone.

Observed TX at firmware uptime 911 ms:

```text
FD FC FB FA 04 00 FF 00 01 00 04 03 02 01
```

At uptime 931 ms, the parser reported a 14-byte configuration-family frame,
command `0x00FF`, apparent status `0x0001`, payload length zero. Thus RX parsing
occurred approximately 20 ms after TX API acceptance; this is not wire latency.

| Field | Actual TX dump | RX evidence available |
| --- | --- | --- |
| Header | FD FC FB FA | Parser recognized configuration header; no raw dump |
| Body length | 04 00 | Parser total length 14 implies body 4 |
| Command | FF 00 | Decoded command 0x00FF |
| Remaining body | 01 00 (ENABLE parameter) | Decoded as status 0x0001 by old ACK path |
| Footer | 04 03 02 01 | Old parser accepted footer; no raw dump |

The fields are consistent with echo. The table is a comparison of TX bytes
and RX *decoded metadata*, not a captured RX byte sequence. Byte-for-byte raw
comparison is unavailable. No other parsed RX frame appears. The report at
~4.07 s counted 14 RX bytes, zero parser errors, four timeouts. Later radar
snapshots through uptime 31071 ms remained UART invalid, with no valid normal
measurement logged. The 14-byte total is the counter at report time, not an
independent raw-byte count over the whole monitor interval.

ENABLE retried once, then END retried once. VERSION was never sent. END mode
exit was not confirmed. The 17 logged telemetry payloads all had
`radar_uart_valid=false`. OUT remained low; physical OUT transitions were not
exercised.

Flash log confirms ESP32-S3 revision v0.2 and verified application write;
that does not establish radar communication. The saved run metadata records
SIGINT at 30.0003 s and forced shutdown at 33.0161 s. This investigation only
reads those old files; it performs no additional hardware session.

## B. Root cause analysis

Two software issues are confirmed:

1. **Misleading classification:** configuration header alone selected the ACK
   path and logged `type=ACK valid=1`. It did not require ACK bit/command or
   status/payload success before that log. ENABLE parameter 0x0001 could be
   misleadingly displayed as NACK status. The self-test itself required exact
   expected command `0x01FF`, so it did **not** falsely pass the 0x00FF input.
2. **Deadline ordering defect:** old worker called state-machine timeout logic
   before reading the UART. If a matching ACK was already buffered when a
   delayed worker resumed past the deadline, the state could move WAIT→SEND,
   then ignore the queued ACK because it was no longer in WAIT. A regression
   reproduces the boundary with the actual worker iteration and fake UART.
   The new iteration reads/processes bytes before timeout evaluation.

Neither explains why this run's only decoded command was 0x00FF. There is no
confirmed root cause for the absent radar ACKs, no confirmed physical echo,
and no demonstrated mismatch in the configured protocol. Do not substitute
0x00FF for 0x01FF or accept the echoed ENABLE parameter as ACK status.

## C. UART driver and routing review

Application source has exactly one `uart_write_bytes` and one
`uart_read_bytes` call, both in the radar driver. No UART consumer elsewhere
in `main`, application loopback call, inversion call, manual GPIO matrix
routing, TX→RX buffer copy or RX→TX echo routine was found. GPIO17/18 appear
as distinct radar pins; no other sensor mapping uses them.

`uart_driver_install`: RX 4096 bytes, TX 256 bytes. `uart_param_config`:
256000, 8 data bits, parity disabled, 1 stop bit, no hardware flow control,
default UART source clock. `uart_set_pin(UART1, GPIO17, GPIO18, unchanged,
unchanged)` routes separate TX/RX signals. Public mapping stays unchanged.
The bounded new event queue has 16 entries and contains ISR metadata only.

Installed ESP-IDF 6.1 source was inspected:

- `components/esp_driver_uart/src/uart.c`: `uart_module_enable` resets UART1
  registers on first enable (console UART is configured as UART0).
  `_uart_set_pin6` routes TX and RX to their separate signal indices with
  inversion false. The same-pin branch is not selected by 17 != 18.
- `components/esp_hal_uart/uart_hal.c`: `uart_hal_init` selects normal UART,
  8N1 and no flow control.
- `components/esp_hal_uart/esp32s3/include/hal/uart_ll.h`:
  `uart_ll_set_loop_back` writes the loopback bit; the application does not
  call this path. There is no evidence of software enabling internal loopback.

This is source-level evidence, not a runtime register/GPIO-matrix readback.
No speculative loopback-disable or pin-routing change has been added.

## D. Parser and concurrency review

Configuration and measurement frames have distinct headers/footers. Length is
LE16; command is LE16; ACK status is LE16. Measurement lengths 13/35 select
normal/engineering data, internal AA/55/00 and target range are checked.
ACK success bodies require lengths 8/12/4 for 01FF/01A0/01FE, respectively.
Firmware payload is type LE16, main-version pair and subversion LE32.
Unexpected ACKs do not advance state; NACK/invalid success payload triggers
finite cleanup. Hi-Link V1.09 sections 2.1, 2.2.1, 2.2.2 and 2.2.8 agree with
these command IDs and sizes. Its firmware-type byte-order inconsistency is
already covered by a separate test and cannot explain the ENABLE failure.

New parser classification:

- `COMMAND_ECHO`: entire RX frame equals a fully API-accepted TX frame stored
  in the bounded six-frame diagnostic history. This is a byte comparison,
  not evidence that radar received or generated the bytes.
- `ACK`: command high byte is 01 and a status word exists; separate
  `semantic_valid` checks expected command, status and payload.
- `MEASUREMENT`: validated measurement family only.
- `UNKNOWN`: framed configuration data lacking ACK identity or exact echo.
- `INVALID`: malformed frame or ACK lacking required status bytes.

Body minimum is now 2 for configuration-family framing so VERSION/END echoes
(12 total bytes) can be recognized without pretending they contain ACK status.
Malformed lengths/footer resynchronize one byte at a time. Partial frames
are retained; all complete frames drain. Valid frame payloads are not scanned
for embedded ACK headers. A complete frame is consumed once. No measurement
is fabricated from an ACK/echo.

Concurrency: stream, TX history, test state and diagnostic counters belong to
one RX task. The safety loop accesses only the locked measurement snapshot,
RX chunk count and last-valid timestamp. Snapshot pending is published after
copy under the same lock; no uninitialized snapshot use was found. All ACK
handling is in that worker, so an ACK arriving during TX cannot be parsed
before the subsequent WAIT assignment; it remains queued until the read.
No runtime RX ring flush is called. Init-time FIFO resets are inside the IDF
configuration path, before self-test begins.

Timeout is still 750 ms, with one retry. At 256000 baud a 14-byte command
occupies ~0.55 ms and an 18-byte ACK ~0.70 ms on an 8N1 wire; RTOS tick is
10 ms. The old log's timeout spacing is finite, about 760 ms. This is ample
for wire time, but the protocol does not provide evidence here of a universal
750 ms response guarantee; module startup/processing delays remain unmeasured.
The unrelated command parsed at 931 ms is not a delayed valid ACK.

## E. Why no normal measurements?

Confidence below applies to each proposed explanation, not a measured probability.

| Hypothesis | Supporting evidence | Contradicting evidence / limit | Verification required | Confidence |
| --- | --- | --- | --- | --- |
| A. Radar not emitting | No accepted measurements throughout monitor | No capture at radar TX; frame rejection could hide data | Observe radar TX independently at correct baud | Unresolved |
| B. Serial/baud mismatch | No valid ACK/measurement | ESP config matches documented default; one structured 14-byte reception | Measure bit timing and verify actual radar baud | Low–medium |
| C. Parser rejects measurements | Old ACK-family classification was misleading | Normal parser fixtures pass; only 14 bytes counted by ~4 s and no malformed-measurement log | Bounded raw RX plus independent wire capture | Low for known 23/45-byte formats |
| D. RX sees TX echo | 14-byte RX metadata matches ENABLE fields, ~20 ms later | No raw RX; subsequent TX retries have no recorded matching frames | Compare raw full frames; inspect both UART wires | Medium suspicion, unconfirmed |
| E. Command mode suppresses reporting | ENABLE sent; END never acknowledged | No successful ENABLE ACK; mode cannot be established | Capture before ENABLE and after END; passive TX observation | Unresolved |
| F. RX task/buffer loses data | Old driver did not collect UART overflow events; deadline ordering defect confirmed | One consumer, bounded 4 KiB RX ring, frequent reads, no parser overflow reported | New event counters and raw ingress capture under runtime load | Low–medium, no observed overflow proof |
| G. Physical connection/power/routing problem | Both ACKs and measurements absent; possible echo | OUT low is valid idle and proves little; wiring not inspected | Verify common ground, crossed RX/TX, power and actual GPIO/module signals | Plausible, unconfirmed |

Zero parser errors in the old log only means no recognized malformed frame was
counted. It excludes neither arbitrary noise skipped during resynchronization
nor UART hardware overflow, because the old event queue was disabled.

## F. Fixes and instrumentation

Changed production files: `main/src/ld2410_radar.c` and
`main/include/diagnostic_config.h`. Test changes are under `tests/ld2410/`.
No GPIO, credentials, other sensors, risk engine or telemetry-schema changes.
Recoverable pre-edit files: `/tmp/ld2410-investigation-backup/`.

- Exact echo classifier and corrected structural versus semantic logging.
- State-transition logging via one state setter, including retries and cleanup.
- Worker now processes queued RX before evaluating timeout.
- Raw RX logged immediately after the read and before parsing/stream copying;
  each dump is at most 64 bytes, total raw RX dump budget 1024 bytes, first
  8 seconds only. `read_len`, `dump_len`, omitted byte count and relative µs
  are explicit. Full parsed-frame dumps are separately bounded to 32 frames,
  each at most 74 bytes, in the same window. Truncation is never represented
  as a full capture. Initial short ACK/echo frames fit these limits.
- TX logs retain exact bytes supplied to the API, requested/accepted sizes and
  relative µs. These remain API-level observations, not wire measurements.
- UART FIFO overflow/ring-full/frame/parity/break events counted using the
  same task. Stream partial state resets on overflow metadata; RX ring is
  not flushed. Event queue is bounded and can itself lose metadata if full;
  counters are observed events, not proof that every event was retained.
- `LD2410_UART_DIAGNOSTICS_ENABLED=0` removes raw/frame/state diagnostic trace;
  self-test enable remains separately controlled. No new radar command.

## G. Tests and build

Actual commands:

```sh
tests/ld2410/run.sh
source /home/cuongdso/esp/esp-idf/export.sh
idf.py build
```

Host tests run against the production driver with UART, clock, GPIO and queue
stubs. PASS in four runs: strict `-Wall -Wextra -Werror -fanalyzer`, self-test
disabled, diagnostics disabled, and ASan/UBSan (leak checking disabled for
ptrace compatibility).

Regressions cover `RX_EXACT_TX_FRAME_MUST_NOT_BE_ACCEPTED_AS_ACK` for all three
TX commands at all split boundaries, exact echo counter once-only behavior,
unknown command without TX evidence, missing ACK status, echo+valid ACK in one
batch, queued ACK beyond deadline before timeout processing, UART overflow
metadata and recovery, full 256-byte ingress chunks, raw byte/time budgets.
Existing encoding/status/payload/measurement/interleaving/noise/cleanup/OUT
fallback/stale snapshot tests still pass. Host lock stubs do not simulate
cross-core scheduling; runtime concurrency validation remains pending.

ESP-IDF 6.1 ESP32-S3 build PASS. Final application image is `0xF5870`
(1,005,680) bytes; `0xA790` (42,896) bytes remain in the 1 MiB app partition.
No compiler warnings/errors in the final build; the partition-nearly-full
warning remains (4% free). Full log: `/tmp/ld2410-investigation-build.log`. `sdkconfig` and partition binary checksums remain
unchanged. Offline image generation is the only esptool invocation by build;
no device access, flash, monitor or UART commands were executed in this task.

## H. Next hardware test — proposal only

After separate authorization, make one bounded boot capture of this diagnostic
build; retain RAW_TX/RAW_RX, state transitions, semantic validity and UART event
counters. Simultaneously observe ESP GPIO17 and GPIO18/radar TX with an
independent 256000-baud 8N1 logic capture if available. Check idle reporting
before commands and after END. Compare full bytes and timing, not ACK labels.
If exact echo is captured, distinguish physical coupling/wiring from peripheral
routing with runtime GPIO/UART register evidence and a controlled isolation
check. Do not reroute GPIOs, change radar settings, or accept wrong ACK IDs.

No proposed hardware test was performed here.

## I. Final verdict

**INSUFFICIENT EVIDENCE — ADDITIONAL RAW RX CAPTURE REQUIRED** for the hardware
ACK/measurement failure. Software diagnostic classification and the queued-ACK
ordering defect are confirmed and corrected; neither is established as the
cause of the physical run's 0x00FF reception. Do not mark physical TX echo,
protocol mismatch, wiring fault or bidirectional UART success as confirmed.

Protocol reference:
https://h.hlktech.com/download/HLK-LD2410C-24G/1/LD2410C%20%E4%B8%B2%E5%8F%A3%E9%80%9A%E4%BF%A1%E5%8D%8F%E8%AE%AE%20V1.09.pdf
