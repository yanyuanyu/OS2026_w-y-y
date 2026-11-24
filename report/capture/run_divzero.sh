#!/bin/bash
set -euo pipefail
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin
cd "$(dirname "$0")/../../code"
python3 tests/divzero/run.py
