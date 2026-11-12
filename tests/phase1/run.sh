#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d /tmp/pccc-phase1-tests.XXXXXX)
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -pthread -Itests/phase1/stubs -Imain/include)
cc "${flags[@]}" main/src/event_buffer.c main/src/mqtt_delivery.c tests/phase1/test_events.c -o "$out/events"
cc "${flags[@]}" main/src/cloud_codec.c main/src/risk_engine.c tests/wp01/fake_latch_store.c tests/phase1/test_codec.c -lm -o "$out/codec"
cc "${flags[@]}" tests/phase1/test_journal.c -o "$out/journal"
cc "${flags[@]}" tests/phase1/test_policy.c -o "$out/policy"
cc "${flags[@]}" tests/phase1/test_wifi.c -o "$out/wifi"
ASAN_OPTIONS=detect_leaks=0 "$out/wifi"
ASAN_OPTIONS=detect_leaks=0 "$out/policy"
ASAN_OPTIONS=detect_leaks=0 "$out/journal"
ASAN_OPTIONS=detect_leaks=0 "$out/events"
ASAN_OPTIONS=detect_leaks=0 "$out/codec"
cc "${flags[@]}" -std=gnu11 main/src/cloud_codec.c main/src/event_buffer.c main/src/mqtt_delivery.c main/src/risk_engine.c tests/wp01/fake_latch_store.c tests/phase1/test_network.c -lm -o "$out/network"
for scenario in 0 1 2 3 4 5 6 7 8 9 10 11 12;do ASAN_OPTIONS=detect_leaks=0 "$out/network" "$scenario";done
mkdir "$out/fixtures"
names=(normal warning_gas emergency emergency_latched sensor_fault recovery_normal reconnect_unsynced boot_unsynced health attributes)
for i in {0..9};do ASAN_OPTIONS=detect_leaks=0 "$out/codec" "$i" > "$out/fixtures/${names[$i]}.json";done
printf 'Phase1 test outputs: %s\n' "$out"
