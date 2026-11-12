#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(mktemp -d /tmp/oled-ui-tests.XXXXXX)"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
 -I"$root/main/include" -I"$root/tests/oled/stubs" \
 -I"$root/managed_components/nopnop2002__ssd1306" \
 "$root/main/src/oled_ui.c" "$root/main/src/risk_engine.c" "$root/tests/wp01/fake_latch_store.c" "$root/tests/oled/test_ui.c" -lm -o "$build_dir/test"
(cd "$build_dir" && ASAN_OPTIONS=detect_leaks=0 ./test)
printf 'Framebuffer PBM previews: %s\n' "$build_dir"
