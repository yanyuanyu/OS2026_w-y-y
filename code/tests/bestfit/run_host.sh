#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source_root="${SOURCE_ROOT:-$(cd "$here/../.." && pwd)}"
build="$here/../../evidence/bestfit-host"
mkdir -p "$build"
echo '=== Current environment ==='
uname -sr
gcc --version | head -n 1
echo '=== Compile real source with AddressSanitizer + UndefinedBehaviorSanitizer ==='
gcc -std=gnu99 -g -O1 -Wall -Wextra -Werror -Wno-unused-variable \
    -Wno-unused-but-set-variable -Wno-sign-compare \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$here/host_headers" -I"$source_root/kern/mm" -idirafter "$source_root/libs" \
    "$here/host_harness.c" -o "$build/best_fit_host"
echo 'COMPILE PASS'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$build/best_fit_host" "$@"
