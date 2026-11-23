#!/bin/bash
set -o pipefail
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin
cd "$(dirname "$0")/../../code" || exit 1
python3 tests/c/dual_gdb.py || exit $?
printf '\n$ grep privilege transitions and helpers in actual GDB logs\n'
grep -E '^(ECALL before:|ECALL after:|SRET before:|SRET after:|#0|#1)' ../report/logs/dual_host_gdb.log
printf '\n$ grep guest PC and return values\n'
grep -E '^(pc |a0 |sepc |scause )' ../report/logs/dual_guest_gdb.log
