#!/bin/bash
set -o pipefail
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin
cd "$(dirname "$0")/../../code" || exit 1
build_tmp=$(mktemp -d /tmp/lab5-abc-build-XXXXXX) || exit 1
case "$build_tmp" in /tmp/lab5-abc-build-*) ;; *) exit 1;; esac
trap 'rm -rf -- "$build_tmp"' EXIT
tar --exclude=./obj --exclude=./bin --exclude=./.git --exclude=./tests/a/out -cf - . | tar -C "$build_tmp" -xf -
echo '$ make -j2 (clean temporary copy)'
make -C "$build_tmp" -j2
rc=$?
printf '\nBUILD_EXIT=%s\n' "$rc"
exit "$rc"
