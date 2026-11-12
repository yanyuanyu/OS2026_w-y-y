#!/bin/bash
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d /tmp/lab4-a-test-XXXXXX)
trap 'rm -rf "$out"' EXIT
flags="-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -O2 -g -std=gnu99 -fno-builtin -nostdinc -fno-stack-protector -ffunction-sections -fdata-sections -Wall"
inc="-Ilibs -Ikern/mm -Ikern/debug -Ikern/driver -Ikern/sync -Ikern/trap -Ikern/process -Ikern/schedule"
objects=""
for f in kern/init/entry.S libs/*.c kern/libs/*.c kern/debug/*.c kern/driver/*.c kern/mm/*.c kern/trap/trap.c tools/test_lab4.c tests/a/memory_test.c; do
 obj="$out/$(echo "$f" | tr / _).o"
 riscv64-unknown-elf-gcc $flags $inc -c "$f" -o "$obj"
 objects="$objects $obj"
done
riscv64-unknown-elf-ld -m elf64lriscv -nostdlib --gc-sections -T tools/kernel.ld -o "$out/kernel.elf" $objects
riscv64-unknown-elf-objcopy -O binary "$out/kernel.elf" "$out/kernel.bin"
for mem in 64 128 256; do
 echo "=== ${mem}M ==="
 timeout 30s qemu-system-riscv64 -machine virt -m "${mem}M" -nographic -bios default -kernel "$out/kernel.bin" > "$out/result.log" 2>&1
 grep -aE 'Test PMM|Test A |PART A PASS' "$out/result.log"
 grep -aq 'PART A PASS' "$out/result.log"
done
