"""Run acceptance against this exact tree; archive raw output and source hashes."""
from pathlib import Path
import subprocess, os, sys, hashlib, json, uuid, platform, shutil
from datetime import datetime, timezone

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'evidence/acceptance'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    state={'run_id':str(uuid.uuid4()),'started_utc':datetime.now(timezone.utc).isoformat(),'status':'RUNNING','steps':[]}
    summary=OUT/'summary.json'
    def save():
        tmp=summary.with_suffix('.tmp');tmp.write_text(json.dumps(state,indent=2,ensure_ascii=False));tmp.replace(summary)
    save()
    def run(name,command,timeout=180):
        print('START '+name,flush=True)
        result=subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
        (OUT/(name+'.log')).write_text('$ '+' '.join(command)+'\n'+result.stdout)
        print(result.stdout[-2200:],flush=True)
        state['steps'].append({'name':name,'returncode':result.returncode,'log':name+'.log'});save()
        if result.returncode:raise RuntimeError(name+' failed')
    try:
        environment={'platform':platform.platform(),'tools':{}}
        for tool in ('gcc','riscv64-unknown-elf-gcc','qemu-system-riscv64','make','riscv64-unknown-elf-gdb'):
            path=shutil.which(tool)
            environment['tools'][tool]={'path':path,'version':subprocess.check_output([tool,'--version'],text=True).splitlines()[0]}
        (OUT/'environment.json').write_text(json.dumps(environment,indent=2))
        print(json.dumps(environment,indent=2),flush=True)
        source={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('kern','libs','tools','tests') for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
        source['Makefile']=hashlib.sha256((ROOT/'Makefile').read_bytes()).hexdigest()
        (OUT/'source_hashes.json').write_text(json.dumps(source,indent=2,ensure_ascii=False))
        run('bestfit-host',['bash','tests/bestfit/run_host.sh'])
        run('buddy-host',['python3','tools/test_buddy_host.py'])
        run('firstfit-tail',['python3','tools/test_firstfit_tail.py'])
        run('kernel-matrix',['python3','tools/verify_c.py'])
        run('slub',['python3','tools/test_slub.py'])
        run('page-init',['python3','tools/test_page_init.py'])
        run('guest-gdb',['python3','tools/debug_guest.py'])
        run('evidence-state',['python3','-m','unittest','discover','-s','tests','-p','test_evidence.py'])
        os.environ['GRADE_EVIDENCE']=str(OUT/'grade-serial.log')
        run('grade',['make','grade'])
        assert 'Total Score: 20/20' in (OUT/'grade.log').read_text()
        serial=(OUT/'grade-serial.log').read_text()
        assert '[Test 1] Page Init: PASS' in serial and '[Test 2] Best-Fit: PASS' in serial
        state['status']='PASS'
    except Exception as exc:
        state['status']='FAIL';state['error']=str(exc);save();raise
    else:
        state['finished_utc']=datetime.now(timezone.utc).isoformat();save()
        print('FINAL ACCEPTANCE PASS run_id='+state['run_id'],flush=True)

if __name__=='__main__':main()
