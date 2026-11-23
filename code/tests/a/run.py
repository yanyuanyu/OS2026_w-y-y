#!/usr/bin/env python3
"""Build only A's real sources, then run the Sv39 kernel under QEMU (WSL/Linux)."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
out = root / 'tests/a/out'
out.mkdir(parents=True, exist_ok=True)
os.chdir(root)
prefix = os.environ.get('GCCPREFIX', 'riscv64-unknown-elf-')
includes = ['libs', 'kern/mm', 'kern/debug', 'kern/driver', 'kern/sync',
            'kern/process', 'kern/schedule', 'kern/trap', 'kern/syscall']
flags = ['-mcmodel=medany', '-march=' + os.environ.get('RISCV_ARCH', 'rv64imac'), '-mabi=lp64',
         '-O2', '-g', '-std=gnu99', '-fno-builtin', '-nostdinc',
         '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
         '-Wall', '-Wno-unused', *['-I' + p for p in includes]]
sources = ['kern/init/entry.S', 'kern/mm/default_pmm.c', 'kern/mm/pmm.c',
           'kern/mm/vmm.c', 'kern/mm/kmalloc.c', 'kern/driver/dtb.c',
           'kern/driver/intr.c', 'kern/driver/console.c', 'kern/libs/stdio.c',
           'libs/string.c', 'libs/printfmt.c', 'libs/hash.c',
           'kern/schedule/sched.c', 'kern/process/switch.S', 'tests/a/test_a.c', 'tests/a/store_trap.S']
objects = []
for source in sources:
    obj = out / (source.replace('/', '_') + '.o')
    subprocess.run([prefix + 'gcc', *flags, '-c', source, '-o', str(obj)], check=True)
    objects.append(str(obj))
kernel = out / 'kernel'
subprocess.run([prefix + 'ld', '-m', 'elf64lriscv', '-nostdlib', '--gc-sections',
                '--wrap=kmalloc', '-T', 'tools/kernel.ld', '-o', str(kernel), *objects], check=True)
subprocess.run([prefix + 'objcopy', '-O', 'binary', str(kernel), str(out / 'kernel.bin')], check=True)
for memory in (64, 128, 256):
    cmd = ['qemu-system-riscv64', '-machine', 'virt', '-smp', '1', '-m', str(memory),
           '-nographic', '-bios', 'default', '-kernel', str(out / 'kernel.bin')]
    try:
        result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=20)
    except subprocess.TimeoutExpired as exc:
        captured = exc.stdout or b''
        if isinstance(captured, bytes):
            captured = captured.decode(errors='replace')
        (out / f'qemu-{memory}.log').write_text(captured)
        print(captured + '\n[A] FAIL QEMU timeout', flush=True)
        sys.exit(1)
    (out / f'qemu-{memory}.log').write_text(result.stdout)
    print(f'=== {memory} MiB ===\n{result.stdout}', flush=True)
    if result.returncode or '[A] ALL PASS' not in result.stdout or 'FAIL' in result.stdout:
        sys.exit(1)
