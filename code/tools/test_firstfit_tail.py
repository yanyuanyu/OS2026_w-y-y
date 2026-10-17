"""Compile real First-Fit and check insertion at the list tail."""
from pathlib import Path
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT/'kern/mm/default_pmm.c')
parser.add_argument('--expect-failure', action='store_true')
args = parser.parse_args()
out = ROOT/'evidence/firstfit-host'
out.mkdir(parents=True, exist_ok=True)
# A source-local include lets the same harness exercise an old source snapshot.
(out/'harness.c').write_text((ROOT/'tests/firstfit_tail.c').read_text().replace('"default_pmm.c"', '"'+str(args.source.resolve())+'"'))
subprocess.run(['gcc', '-c', '-g', str(ROOT/'tests/host_io.c'), '-o', str(out/'io.o')], check=True)
command = ['gcc', '-std=gnu99', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-sign-compare', '-fno-builtin', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-Ilibs', '-Ikern/mm', '-Ikern/debug', str(out/'harness.c'), str(out/'io.o'), '-o', str(out/'firstfit')]
subprocess.run(command, cwd=ROOT, check=True)
try:
    result = subprocess.run([str(out/'firstfit')], text=True, capture_output=True, timeout=3)
except subprocess.TimeoutExpired:
    if not args.expect_failure:
        raise
    print('Original First-Fit: expected timeout in tail insertion (regression reproduced)')
else:
    print(result.stdout+result.stderr, end='')
    if args.expect_failure:
        assert result.returncode == 99 and 'assertion failed: le->next->prev == le && le->prev->next == le' in result.stderr, 'Expected the specific broken-link assertion'
        print('Original First-Fit: expected broken-link assertion (regression reproduced)')
    else:
        assert result.returncode == 0
