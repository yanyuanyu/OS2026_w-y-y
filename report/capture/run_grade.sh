#!/bin/bash
set -o pipefail
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin
cd "$(dirname "$0")/../../code" || exit 1
make grade GRADE_TMP=/tmp/lab5-abc-capture-grade
