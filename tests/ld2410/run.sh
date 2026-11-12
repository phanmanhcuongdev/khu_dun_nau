#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out_dir=$(mktemp -d /tmp/ld2410-tests.XXXXXX)
cc -std=c11 -Wall -Wextra -Werror -fanalyzer -Itests/ld2410/stubs -Imain/include tests/ld2410/test_ld2410.c -o "$out_dir/test"
"$out_dir/test"
cc -std=c11 -Wall -Wextra -Werror -DLD2410_UART_SELFTEST_ENABLED=0 -Itests/ld2410/stubs -Imain/include tests/ld2410/test_ld2410.c -o "$out_dir/disabled"
"$out_dir/disabled"
cc -std=c11 -Wall -Wextra -Werror -DLD2410_UART_DIAGNOSTICS_ENABLED=0 -Itests/ld2410/stubs -Imain/include tests/ld2410/test_ld2410.c -o "$out_dir/no-diagnostics"
"$out_dir/no-diagnostics"
cc -std=c11 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/ld2410/stubs -Imain/include tests/ld2410/test_ld2410.c -o "$out_dir/sanitized"
# LeakSanitizer cannot run under the sandbox ptrace; retain ASan and UBSan.
ASAN_OPTIONS=detect_leaks=0 "$out_dir/sanitized"
printf 'Test executables: %s\n' "$out_dir"
