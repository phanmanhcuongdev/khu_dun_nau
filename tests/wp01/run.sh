#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d /tmp/wp01-tests.XXXXXX)
flags=(-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/wp01/stubs -Imain/include)
cc "${flags[@]}" main/src/risk_engine.c tests/wp01/fake_latch_store.c tests/wp01/test_recovery.c -o "$out/recovery"
cc "${flags[@]}" tests/wp01/test_store.c -o "$out/store"
cc "${flags[@]}" tests/wp01/test_touch.c -o "$out/touch"
cc "${flags[@]}" main/src/risk_engine.c main/src/local_alarm_controller.c tests/wp01/fake_latch_store.c tests/wp01/test_state.c -o "$out/state"
for test in recovery store touch state;do ASAN_OPTIONS=detect_leaks=0 "$out/$test";done
printf 'WP01 test binaries: %s\n' "$out"
