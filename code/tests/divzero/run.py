#!/usr/bin/env python3
"""Run the real DIVW sample at four user-program optimization levels."""
from pathlib import Path
import json
import os
import re
import select
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
LOG = ROOT.parent / 'report' / 'logs'
LOG.mkdir(parents=True, exist_ok=True)
ENV = os.environ.copy()
GCCPREFIX = ENV.get('GCCPREFIX', 'riscv64-unknown-elf-')
results = []
compiler = subprocess.check_output([GCCPREFIX + 'gcc', '--version'], env=ENV, text=True).splitlines()[0]
qemu = ENV.get('QEMU_BIN', 'qemu-system-riscv64')
qemu_version = subprocess.check_output([qemu, '--version'], env=ENV, text=True).splitlines()[0]
print(compiler, flush=True)
print(qemu_version, flush=True)
with tempfile.TemporaryDirectory(prefix='lab5-divzero-') as tmp:
    dst = Path(tmp) / 'code'
    shutil.copytree(ROOT, dst, ignore=shutil.ignore_patterns('obj', 'bin', 'out', '__pycache__', '.git'))
    makefile = (dst / 'Makefile').read_text()
    original = '$(call add_files_cc,$(call listf_cc,$(USRCDIR)),uprog,$(UCFLAGS) -O0)'
    assert original in makefile, 'User compilation rule changed; update the optimization matrix.'
    for opt in ('-O0', '-O1', '-O2', '-Os'):
        (dst / 'Makefile').write_text(makefile.replace(original, original.replace('-O0', opt)))
        subprocess.run(['make', 'clean'], cwd=dst, env=ENV, check=True, stdout=subprocess.DEVNULL)
        build = subprocess.run(['make', '-j2', 'build-divzero', 'GCCPREFIX=' + GCCPREFIX], cwd=dst, env=ENV,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        tag = 'divzero_' + opt[1:]
        (LOG / (tag + '_build.log')).write_text(build.stdout)
        if build.returncode:
            raise RuntimeError(build.stdout[-4000:])
        assembly = subprocess.check_output([GCCPREFIX + 'objdump', '-d', 'obj/__user_divzero.out'],
                                           cwd=dst, env=ENV, text=True)
        (LOG / (tag + '_disassembly.log')).write_text(assembly)
        main = assembly.split('<main>:', 1)[1].split('\n\n', 1)[0]
        assert re.search(r'\bdivw\b', main), tag + ': DIVW missing from main'
        proc = subprocess.Popen([qemu, '-machine', 'virt', '-m', '128', '-nographic', '-bios', 'default',
                                 '-kernel', 'bin/ucore.img'], cwd=dst, env=ENV,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        data = b''
        deadline = time.monotonic() + 20
        try:
            while time.monotonic() < deadline:
                ready, _, _ = select.select([proc.stdout], [], [], 0.2)
                if ready:
                    chunk = os.read(proc.stdout.fileno(), 65536)
                    if not chunk:
                        break
                    data += chunk
                    if b'init check memory pass.' in data or b'kernel panic' in data:
                        break
                if proc.poll() is not None:
                    break
        finally:
            proc.terminate()
            try:
                tail, _ = proc.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                tail, _ = proc.communicate()
            data += tail
        output = data.decode('utf-8', errors='replace')
        (LOG / (tag + '.log')).write_text(output)
        passed = ('value is -1.' in output and 'init check memory pass.' in output
                  and 'kernel panic' not in output)
        results.append({'optimization': opt, 'divw_in_main': True, 'value': -1 if passed else None, 'passed': passed})
        print('[DIVZERO] ' + opt + ': DIVW present; ' + ('value is -1.; PASS' if passed else 'FAIL'), flush=True)
        if not passed:
            raise RuntimeError(output[-4000:])
(LOG / 'divzero_matrix.json').write_text(json.dumps({'compiler': compiler, 'qemu': qemu_version, 'cases': results}, indent=2))
print('DIVZERO_MATRIX_ALL_PASS (4 cases)', flush=True)
