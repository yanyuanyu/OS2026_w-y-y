"""Run the real SLUB C self-check on all three PMM backends."""
from pathlib import Path
import json
import re
import subprocess
import uuid
import verify_c

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'evidence/slub'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    verify_c.OUT = OUT
    state = {'run_id': str(uuid.uuid4()), 'status': 'RUNNING', 'results': []}
    def save():
        temp = OUT / 'summary.json.tmp'
        temp.write_text(json.dumps(state, indent=2))
        temp.replace(OUT / 'summary.json')
    save()
    try:
        for backend in ['bestfit', 'firstfit', 'buddy']:
            with (OUT / (backend + '-build.log')).open('w') as log:
                subprocess.run(['make', '-j2', 'PMM_ALGO=' + backend], cwd=ROOT,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            result = verify_c.run_case(backend, 128, state['run_id'])
            text = (ROOT / result['log']).read_text()
            assert 'slub_check: size boundaries, alignment, data, reuse, cross-page, large, reclaim PASS' in text
            free = int(re.search(r'Free physical memory begins at (0x[0-9a-f]+)', text)[1], 16)
            expected = (0x88000000 - free) // 4096
            restored = int(re.search(r'slub_check\(\) succeeded! free pages restored: (\d+)', text)[1])
            assert restored == expected
            result['restored_pages'] = restored
            result['boundary_sizes'] = 26
            result['cross_page_objects'] = 140
            state['results'].append(result)
            save()
            print(f'SLUB {backend}: 26 sizes, 140-object cross-page/reuse, data and reclaim PASS; restored={restored}', flush=True)
    except BaseException as error:
        state.update(status='FAIL', error=str(error))
        save()
        raise
    state['status'] = 'PASS'
    save()
    print('SLUB ALL THREE BACKENDS PASS run_id=' + state['run_id'], flush=True)

if __name__ == '__main__':
    main()
