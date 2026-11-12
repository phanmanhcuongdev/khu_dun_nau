# Phase 1 regression suite

`bash tests/phase1/run.sh` compiles production event buffer, delivery ledger, journal, serializer and network-owner source with ASan/UBSan, pthread producers/consumers and injected ESP-IDF platform functions. No hardware, broker or production Cloud changes are made. Thirteen network scenarios simulate 90 seconds each: delayed ACK, blackhole, pre-ACK disconnect, reconnect storm, callback overflow, client allocation failure, handler registration failure, absent trusted clock, a clock jump, full critical queue with current-state resync, enqueue/outbox rejection, Wi-Fi absent, and delayed Wi-Fi/IP acquisition. The real Wi-Fi initialization source is also tested at twelve failure points with cleanup/reconnect assertions. Queue and task allocation failures are also exercised before every scenario. LeakSanitizer is disabled because the host sandbox uses ptrace; bounded queue allocation cleanup has explicit live-allocation assertions. This is not a long-run heap-leak/real TCP test.

The script prints a temporary directory containing serializer-generated `fixtures/`. Validate both those outputs and the committed fixtures with a host-only Draft 2020-12 validator:

```sh
PYTHONPATH=/tmp/pccc-jsonschema-validator python3 tests/phase1/validate_contract.py
PYTHONPATH=/tmp/pccc-jsonschema-validator python3 tests/phase1/validate_contract.py /tmp/PRINTED_TEST_DIRECTORY/fixtures
python3 tests/phase1/test_migration.py
```

The implementation session installed `jsonschema==4.26.0` under `/tmp/pccc-jsonschema-validator`, outside the firmware. On another host use an isolated virtual environment with that test dependency. It is not an ESP-IDF/runtime dependency. Rebuild schemas/dictionary with `python3 tools/generate_contract_assets.py` after a reviewed contract change. Illustrative fixtures never prove physical sensor readings or Cloud database ingestion.

Other regression suites:

```sh
bash tests/wp01/run.sh
bash tests/oled/run.sh
python3 tests/rgb_status/run.py
bash tests/ld2410/run.sh
python3 tools/release_check.py
```

Power-cut/torn-flash behavior, RF blackholes, certificate-negative tests on MCU, audible/visual actuator behavior and 24/48/72-hour soak remain hardware/staging tests. Broker PUBACK is not ThingsBoard database commit and QoS1 permits duplicates.

Baseline R02 reproduction (expected exit1; original files remain in the private baseline directory):

```sh
cc -std=c11 -pthread -I/tmp/phase1-baseline/main/include -Itests/phase1/stubs /tmp/phase1-baseline/main/src/event_buffer.c tests/phase1/reproduce_r02_legacy.c -o /tmp/r02-before
/tmp/r02-before
```

The new implementation's corresponding regression lives in `test_events.c` and must exit0. The baseline probe deliberately uses the removed peek/drop API and is not linked into production or the new test binary.
