#!/usr/bin/env python3
"""Run full teacher grading on isolated original and boot-compatible copies."""
from pathlib import Path
import subprocess, tempfile, shutil, json
root=Path(__file__).resolve().parents[2]
code=root/'code'
logs=root/'report/integration-abc/logs'
logs.mkdir(parents=True,exist_ok=True)
results={}
def run(name,args,cwd):
    p=subprocess.run(args,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180)
    (logs/(name+'.log')).write_text(p.stdout)
    results[name]=p.returncode
    print(name, p.returncode, flush=True)
    print(p.stdout[-1000:], flush=True)
    return p
with tempfile.TemporaryDirectory(prefix='lab3-abc-') as tmp:
    tmp=Path(tmp)
    for mode in ['original','compatible']:
        target=tmp/mode
        shutil.copytree(code,target,ignore=shutil.ignore_patterns('obj','bin','__pycache__','.qemu*'))
        run(mode+'-inject',['python3','tools/inject.py',str(target)],target)
        if mode=='compatible':
            entry=target/'kern/init/entry.S'
            s=entry.read_text()
            assert s.count('.zero 8 * 511') in (0,1)
            entry.write_text(s.replace('.zero 8 * 511','.zero 8 * 2\n    .quad (0x80000 << 10) | 0xcf # test-only identity transition\n    .zero 8 * 508'))
            grade=target/'tools/grade.sh'
            s=grade.read_text().replace('-device loader,file=bin/ucore.img,addr=0x80200000','-kernel bin/ucore.img')
            grade.write_text(s)
        run(mode+'-grade',['sh','tools/grade.sh'],target)
        for p in target.glob('.qemu.*.out'):
            shutil.copyfile(p,logs/(mode+'-'+p.name.lstrip('.')))
    run('exception-host',['python3','tests/test_exception.py'],code)
(logs/'results.json').write_text(json.dumps(results,indent=2))
assert results['compatible-grade']==0 and results['exception-host']==0,results
