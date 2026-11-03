#!/usr/bin/env python3
"""Run fresh commands in a native terminal for screenshot evidence."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import selectors
import shlex
import subprocess
import sys
import time

REPORT = Path(__file__).resolve().parents[1]
ROOT = REPORT.parent
sys.path.insert(0, str(ROOT / 'code/tests/b'))
import verify_b as v

CASE = sys.argv[1]
OUT = REPORT / 'logs/screenshots'
OUT.mkdir(parents=True, exist_ok=True)
START = datetime.datetime.now().astimezone().isoformat()
print('Lab3 B | 2413682 Yan Yuanyu | ' + CASE, flush=True)
print('Fresh execution: ' + START, flush=True)
print('Scope: B only; A/C entry and boot support are QA fixtures.', flush=True)
COMMANDS = []


def run(argv, cwd=v.WORK, quiet=False):
    print('$ ' + shlex.join(argv), flush=True)
    start = time.monotonic()
    proc = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          timeout=120 if CASE == '06-fresh-acceptance' else 45)
    stem = f'{CASE}-{len(COMMANDS)+1:02d}'
    (OUT / (stem + '.log')).write_text(proc.stdout, encoding='utf-8')
    if not quiet:
        print(proc.stdout, end='', flush=True)
    print('[exit code ' + str(proc.returncode) + ']', flush=True)
    COMMANDS.append({'argv': argv, 'cwd': str(cwd), 'returncode': proc.returncode,
                     'seconds': round(time.monotonic()-start, 3), 'log': stem+'.log'})
    assert proc.returncode == 0
    return proc.stdout


def qemu(target, expected_lines, marker=None):
    argv = ['qemu-system-riscv64', '-machine', 'virt', '-nographic',
            '-bios', 'default', '-kernel', 'bin/ucore.img', '-monitor', 'none',
            '-no-reboot']
    print('$ ' + shlex.join(argv), flush=True)
    print('(Live serial from DTB Init; full OpenSBI output saved in raw log.)', flush=True)
    start = time.monotonic()
    proc = subprocess.Popen(argv, cwd=target, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    output = ''
    shown = None
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if selector.select(0.1):
                chunk = os.read(proc.stdout.fileno(), 65536)
                if chunk:
                    output += chunk.decode('utf-8', errors='replace')
                    if shown is None and 'DTB Init' in output:
                        shown = output.index('DTB Init')
                    if shown is not None:
                        print(output[shown:], end='', flush=True)
                        shown = len(output)
                elif proc.poll() is not None:
                    break
            if proc.poll() is not None:
                tail = proc.stdout.read().decode('utf-8', errors='replace')
                output += tail
                if shown is not None:
                    print(output[shown:], end='', flush=True)
                break
        else:
            raise TimeoutError('QEMU failed to shut down within 30 seconds')
        proc.wait(timeout=3)
        count = sum(line.strip() == '100 ticks' for line in output.splitlines())
        assert count == expected_lines, (count, expected_lines)
        assert proc.returncode == 0
        assert 'check_alloc_page() succeeded!' in output
        assert 'panic' not in output.lower()
        if marker:
            assert marker in output
        print(f'[QEMU exit=0; reports={count}; expected={expected_lines}]', flush=True)
        COMMANDS.append({'argv': argv, 'cwd': str(target), 'returncode': proc.returncode,
                         'seconds': round(time.monotonic()-start, 3), 'tick_lines': count})
    finally:
        selector.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        proc.stdout.close()
        (OUT / (CASE + '-serial.log')).write_text(output, encoding='utf-8')


status = 'FAIL'
try:
    if CASE == '01-build':
        target = v.prepare_kernel('capture_default')
        run(['make', 'clean'], cwd=target, quiet=True)
        run(['make', '-j2', 'DEFS+=-Dsbadaddr=stval'], cwd=target)
    elif CASE == '02-qemu-default':
        qemu(v.WORK / 'capture_default', 10)
    elif CASE == '03-teacher-timer':
        target = v.prepare_kernel('capture_teacher')
        run(['python3', str(v.CODE / 'tools/inject.py'), str(target)], quiet=True)
        run(['make', '-j2', 'DEFS+=-DGRADE_LAB3_TIMER -Dsbadaddr=stval'],
            cwd=target, quiet=True)
        qemu(target, 10, '[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)')
    elif CASE == '04-host-boundaries':
        print('$ python3 ../code/tests/b/verify_b.py (host_tests entry)', flush=True)
        v.host_tests()
        for label in ['default', 'override3']:
            print('--- SHUTDOWN_AFTER=' + ('10' if label == 'default' else '3') + ' ---', flush=True)
            # These files were produced just above by executing the binaries now.
            print((v.LOGS / ('host_' + label + '.log')).read_text(encoding='utf-8'), end='')
        print('SHUTDOWN_AFTER=0: correctly rejected by compiler.', flush=True)
    elif CASE == '05-qemu-override3':
        target = v.prepare_kernel('capture_override3')
        run(['make', '-j2', 'DEFS+=-DSHUTDOWN_AFTER=3 -Dsbadaddr=stval'], cwd=target, quiet=True)
        qemu(target, 3)
    elif CASE == '06-fresh-acceptance':
        run(['python3', str(v.CODE / 'tests/b/verify_b.py')], cwd=ROOT)
    else:
        raise SystemExit('Unknown case: ' + CASE)
    print('CASE PASS: ' + CASE, flush=True)
    status = 'PASS'
finally:
    meta = {'case': CASE, 'started': START, 'status': status, 'commands': COMMANDS,
            'verify_results': v.RESULTS, 'scope': 'B with QA fixtures; not full group make grade'}
    (OUT / (CASE + '.json')).write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding='utf-8')
