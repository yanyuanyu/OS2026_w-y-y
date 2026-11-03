#!/usr/bin/env python3
"""Validate integrated A+B, leaving production C unimplemented. Run inside WSL."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
CODE = ROOT / 'code'
LOGS = ROOT / 'report/integration/logs'
WORK = ROOT.parent / '.qa/AB_integration'
LOGS.mkdir(parents=True, exist_ok=True)
WORK.mkdir(parents=True, exist_ok=True)
os.environ['PATH'] = ('/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:'
                      '/opt/qemu/bin:' + os.environ['PATH'])
RESULTS = {}


def run(name, argv, cwd=WORK, timeout=45, expected=0):
    start = time.monotonic()
    try:
        proc = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or b''
        if isinstance(output, bytes):
            output = output.decode('utf-8', errors='replace')
        (LOGS / (name + '.log')).write_text(output, encoding='utf-8')
        RESULTS[name] = {'command': argv, 'cwd': str(cwd), 'returncode': 124,
                         'seconds': timeout}
        raise RuntimeError(f'{name}: timeout; see logs/{name}.log') from exc
    (LOGS / (name + '.log')).write_text(proc.stdout, encoding='utf-8')
    RESULTS[name] = {'command': argv, 'cwd': str(cwd),
                     'returncode': proc.returncode,
                     'seconds': round(time.monotonic() - start, 3)}
    print(f'{name}: exit={proc.returncode}', flush=True)
    if proc.returncode != expected:
        raise RuntimeError(f'{name}: expected {expected}; see logs/{name}.log')
    return proc.stdout


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def host_tests():
    source = (CODE / 'kern/trap/trap.c').read_text(encoding='utf-8')
    handler = function(source, 'void interrupt_handler(')
    config = source[source.index('#define TICK_NUM'):source.index('static void print_ticks')]
    printer = function(source, 'static void print_ticks()')
    riscv = (CODE / 'libs/riscv.h').read_text(encoding='utf-8')
    macros = '\n'.join(re.findall(r'^#define IRQ_\w+\s+\d+\s*$', riscv, re.M))
    harness = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct trapframe { uintptr_t gpr[32], status, epc, badvaddr, cause; };
static volatile size_t ticks;
static size_t armed, shutdowns, dumps;
static char output[4096];
static void cprintf(const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    size_t used = strlen(output);
    int n = vsnprintf(output + used, sizeof(output) - used, fmt, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(output) - used);
}
static void clock_set_next_event(void) { assert(armed == ticks); armed++; }
static void sbi_shutdown(void) { shutdowns++; }
static void print_trapframe(struct trapframe *tf) { assert(tf != NULL); dumps++; }
'''
    harness += macros + '\n' + config + printer + '\n' + handler
    harness += r'''
static void call_preserving_frame(uintptr_t cause) {
    struct trapframe tf, before;
    memset(&tf, 0xa5, sizeof(tf)); tf.cause = cause; before = tf;
    output[0] = 0;
    interrupt_handler(&tf);
    assert(memcmp(&tf, &before, sizeof(tf)) == 0);
}
int main(void) {
    const uintptr_t flag = (uintptr_t)1 << (sizeof(uintptr_t)*8-1);
    const char *messages[] = {
        "User software interrupt\n", "Supervisor software interrupt\n",
        "Hypervisor software interrupt\n", "Machine software interrupt\n",
        "User timer interrupt\n", NULL, "Hypervisor timer interrupt\n",
        "Machine timer interrupt\n", "User external interrupt\n",
        "Supervisor external interrupt\n", "Hypervisor external interrupt\n",
        "Machine external interrupt\n"
    };
    for (uintptr_t cause = 0; cause < 12; cause++) {
        if (cause == IRQ_S_TIMER) continue;
        for (unsigned marked = 0; marked < 2; marked++) {
            call_preserving_frame(cause | (marked ? flag : 0));
            assert(strcmp(output, messages[cause]) == 0);
            assert(ticks == 0 && armed == 0 && shutdowns == 0 && dumps == 0);
        }
    }
    puts("PASS: 11 non-timer branches with/without interrupt flag; no timer side effects");
    for (unsigned marked = 0; marked < 2; marked++) {
        call_preserving_frame(15 | (marked ? flag : 0));
        assert(strcmp(output, "Unknown interrupt: cause 15\n") == 0);
        assert(dumps == marked + 1 && ticks == 0 && armed == 0);
    }
    puts("PASS: unknown interrupt prints normalized cause and dumps trapframe");
    for (size_t n = 1; n <= SHUTDOWN_AFTER * TICK_NUM; n++) {
        call_preserving_frame(flag | IRQ_S_TIMER);
        assert(ticks == n && armed == n);
        assert(strcmp(output, n % TICK_NUM == 0 ? "100 ticks\n" : "") == 0);
        assert(shutdowns == (n == SHUTDOWN_AFTER * TICK_NUM ? 1 : 0));
        if (n == 99 || n == 100 || n == 199 || n == 200)
            printf("PASS: timer boundary ticks=%zu\n", n);
    }
    printf("PASS: shutdown exactly at %d reports / %zu ticks; every trapframe preserved\n",
           SHUTDOWN_AFTER, (size_t)ticks);
    return 0;
}
'''
    (WORK / 'host_b.c').write_text(harness, encoding='utf-8')
    for label, defs in [('default', []), ('override3', ['-DSHUTDOWN_AFTER=3'])]:
        exe = WORK / ('host_b_' + label)
        run('host_build_' + label, ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra',
                                    '-Werror', *defs, 'host_b.c', '-o', str(exe)])
        run('host_' + label, [str(exe)])
    run('reject_zero', ['gcc', '-std=c11', '-DSHUTDOWN_AFTER=0',
                        '-c', 'host_b.c', '-o', 'invalid.o'], expected=1)


def prepare_kernel(name):
    target = WORK / name
    if target.exists():
        # Only remove this script's known child within its own QA directory.
        assert target.resolve().parent == WORK.resolve()
        shutil.rmtree(target)
    shutil.copytree(CODE, target)
    assembly = target / 'kern/trap/trapentry.S'
    text = assembly.read_text(encoding='utf-8')
    restore = '\n    # TEST SUPPORT ONLY: A has not delivered its module.\n'
    restore += '    LOAD t0, 32*REGBYTES(sp)\n    LOAD t1, 33*REGBYTES(sp)\n'
    restore += '    csrw sstatus, t0\n    csrw sepc, t1\n'
    for n in [1, *range(3, 32), 2]:
        restore += f'    LOAD x{n}, {n}*REGBYTES(sp)\n'
    text = text.replace('    #lab3 function1 YOUR CODE', restore, 1)
    text = text.replace('    #lab3 function1 YOUR CODE',
                        '    # TEST SUPPORT ONLY\n    mv a0, sp\n    jal trap', 1)
    assembly.write_text(text, encoding='utf-8')
    trap = target / 'kern/trap/trap.c'
    text = trap.read_text(encoding='utf-8')
    text = text.replace('//lab3 function2 C YOUR CODE', '''// TEST SUPPORT ONLY: fail on unexpected exceptions; no C implementation.
void exception_handler(struct trapframe *tf) {
    print_trapframe(tf);
    panic("B timer test encountered an unexpected exception");
}''')
    trap.write_text(text, encoding='utf-8')
    # Shared boot dependency, already fixed in the final Lab2 submission:
    # keep the physical PC mapped between satp write and the high-address jump.
    # Apply only in QA; this is not part of B's interrupt handler.
    entry = target / 'kern/init/entry.S'
    text = entry.read_text(encoding='utf-8')
    assert text.count('.zero 8 * 511') == 1
    text = text.replace('.zero 8 * 511',
        '.zero 8 * 2\n    .quad (0x80000 << 10) | 0xcf # QA identity transition\n'
        '    .zero 8 * 508')
    entry.write_text(text, encoding='utf-8')
    return target


def qemu(name, target, seconds, expected=0, patterns=(), tick_lines=None):
    argv = ['qemu-system-riscv64', '-machine', 'virt', '-nographic',
            '-bios', 'default', '-kernel', 'bin/ucore.img', '-monitor', 'none',
            '-no-reboot']
    output = run(name, argv, cwd=target, timeout=seconds, expected=expected)
    for pattern in patterns:
        if pattern not in output:
            raise AssertionError(f'{name}: missing {pattern}')
    if tick_lines is not None:
        count = sum(line.strip() == '100 ticks' for line in output.splitlines())
        assert count == tick_lines, (name, count, tick_lines)
    assert 'kernel panic' not in output
    return output


def kernel_tests():
    includes = ['libs', 'kern/debug', 'kern/driver', 'kern/trap', 'kern/mm']
    run('compile_B_object', ['riscv64-unknown-elf-gcc', '-mcmodel=medany',
         '-std=gnu99', '-Wno-unused', '-Werror', '-fno-builtin', '-Wall', '-O2',
         '-nostdinc', '-fno-stack-protector',
         *['-I' + path for path in includes], '-c', 'kern/trap/trap.c',
         '-o', str(WORK / 'trap_B.o')], cwd=CODE)
    target = prepare_kernel('kernel_default')
    run('kernel_build_default', ['make', '-j2', 'DEFS+=-Dsbadaddr=stval'], cwd=target)
    qemu('kernel_default_shutdown', target, 30,
         patterns=('check_alloc_page() succeeded!', '++ setup timer interrupts'),
         tick_lines=10)
    # Use teacher's original injector and timer source, leaving the originals intact.
    target = prepare_kernel('kernel_teacher_timer')
    run('teacher_inject', ['python3', str(CODE / 'tools/inject.py'), str(target)])
    run('kernel_build_teacher_timer', ['make', '-j2', 'DEFS+=-DGRADE_LAB3_TIMER -Dsbadaddr=stval'],
        cwd=target)
    # The teacher test spins after its PASS at 200 ticks. Interrupts must continue
    # and eventually hit our default shutdown at 1000 ticks.
    qemu('teacher_timer', target, 30,
         patterns=('[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)',),
         tick_lines=10)
    target = prepare_kernel('kernel_override3')
    run('kernel_build_override3', ['make', '-j2', 'DEFS+=-DSHUTDOWN_AFTER=3 -Dsbadaddr=stval'],
        cwd=target)
    qemu('kernel_override3_shutdown', target, 15,
         patterns=('++ setup timer interrupts',), tick_lines=3)


def audit():
    baseline = json.loads((CODE / 'tests/b/teacher_baseline.json').read_text(encoding='utf-8'))
    changed = []
    hashes = {}
    for path in sorted(CODE.rglob('*')):
        if not path.is_file() or path.is_relative_to(CODE / 'tests'):
            continue
        rel = path.relative_to(CODE).as_posix()
        hashes[rel] = hashlib.sha256(path.read_bytes()).hexdigest()
        if hashes[rel] != baseline['files'].get(rel):
            changed.append(rel)
    assert set(hashes) == set(baseline['files']), 'Unexpected source file additions/removals'
    assert changed == ['kern/mm/best_fit_pmm.c', 'kern/mm/pmm.c', 'kern/trap/trap.c', 'kern/trap/trapentry.S'], changed
    for check in baseline['preserved_functions']:
        text = (CODE / check['path']).read_text(encoding='utf-8')
        body = function(text, check['signature'])
        assert hashlib.sha256(body.encode('utf-8')).hexdigest() == check['sha256'], check
    (LOGS / 'source_hashes.json').write_text(json.dumps(hashes, indent=2), encoding='utf-8')
    RESULTS['audit'] = {'changed_files': changed, 'teacher_checks_unchanged': True,
                         'A_integrated': True, 'C_unimplemented_in_delivery': True}
    print('source audit: PASS', flush=True)


if __name__ == '__main__':
    try:
        run('environment', ['bash', '-c', 'uname -a; riscv64-unknown-elf-gcc --version; '
                             'qemu-system-riscv64 --version; gcc --version; python3 --version'])
        audit()
        host_tests()
        kernel_tests()
        RESULTS['status'] = 'PASS'
    except Exception as exc:
        RESULTS['status'] = 'FAIL'
        RESULTS['error'] = str(exc)
        raise
    finally:
        (LOGS / 'summary.json').write_text(json.dumps(RESULTS, ensure_ascii=False,
                                                     indent=2), encoding='utf-8')
    print('B acceptance: PASS (teacher timer 40/40; not a full-group grade)', flush=True)
