#!/bin/bash
set -o pipefail
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin
cd "$(dirname "$0")/../../code" || exit 1
RISCV_ARCH=rv64imac_zicsr_zifencei make test-a | tee ../report/logs/capture_a_raw.log | grep -E "^===|^\[A\]"
