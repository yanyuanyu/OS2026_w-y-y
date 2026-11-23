#!/usr/bin/env python3
"""Integrated user-mode tests against the ABC implementation."""
from pathlib import Path
import os,subprocess,tempfile,shutil,select,time,json,sys,re
ROOT=Path(__file__).resolve().parents[2]
LOG=ROOT.parent/'report/logs';LOG.mkdir(parents=True,exist_ok=True)
ENV=os.environ.copy();ENV['PATH']='/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin'
cases=[('b_lifecycle','b_lifecycle pass.',128),('b_isolation_orphan','b_isolation_orphan pass.',128),
       ('b_execfail','b_execfail pass.',128),('b_execsuccess','b_execsuccess pass.',128),
       ('cowtest','cowtest pass.',128),('c_syscalls','C_SYSCALLS_ALL_PASS',128),
       ('c_cow','C_COW_ALL_PASS',128),('forktree',"I am '1111'",128),
       ('waitkill','kill child1 ok.',128),('abc_exec_edges','ABC_EXEC_EDGES_PASS',128),
       ('cowtest','cowtest pass.',64),('cowtest','cowtest pass.',256),
       ('dirtymodel','DIRTYCOW_MODEL_ALL_PASS',128)]
if len(sys.argv)>1:cases=[x for x in cases if x[0] in sys.argv[1:]]
results=[]
with tempfile.TemporaryDirectory(prefix='lab5-abc-integration-') as tmp:
    dst=Path(tmp)/'code';shutil.copytree(ROOT,dst,ignore=shutil.ignore_patterns('obj','bin','out','__pycache__','.git'))
    for name in ('c_syscalls','c_cow'):
        shutil.copy2(ROOT/'tests/c'/f'{name}.c',dst/'user'/f'{name}.c')
    shutil.copy2(ROOT/'tests/c/register_probe.S',dst/'user/libs/register_probe.S')
    shutil.copy2(ROOT/'tests/integration/abc_exec_edges.c',dst/'user/abc_exec_edges.c')
    for name,token,mem in cases:
        tag=f'{name}_{mem}'
        if name=='dirtymodel':
            shutil.copy2(ROOT/'user/hello.c',dst/'user/dirtymodel.c')
            shutil.copy2(ROOT/'tests/c/kernel_cow_model.c',dst/'kern/mm/dirty_model.c')
            p=dst/'kern/process/proc.c';p.write_text(p.read_text().replace('size_t free_before = nr_free_pages();','extern void c_test_dirtycow_model(void); c_test_dirtycow_model(); size_t free_before = nr_free_pages();'))
        build=subprocess.run(['make','-j2',f'build-{name}'],cwd=dst,env=ENV,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (LOG/(tag+'_build.log')).write_text(build.stdout)
        if build.returncode:raise RuntimeError(build.stdout[-4000:])
        proc=subprocess.Popen(['qemu-system-riscv64','-machine','virt','-m',str(mem),'-nographic','-bios','default','-kernel','bin/ucore.img'],cwd=dst,env=ENV,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        data=b'';deadline=time.monotonic()+20
        try:
            while time.monotonic()<deadline:
                ready,_,_=select.select([proc.stdout],[],[],0.2)
                if ready:
                    chunk=os.read(proc.stdout.fileno(),65536)
                    if not chunk:break
                    data+=chunk
                    if b'init check memory pass.' in data or b'kernel panic' in data:break
                if proc.poll() is not None:break
        finally:
            proc.terminate()
            try:tail,_=proc.communicate(timeout=3)
            except subprocess.TimeoutExpired:proc.kill();tail,_=proc.communicate()
            data+=tail
        out=data.decode('utf-8',errors='replace');(LOG/(tag+'.log')).write_text(out)
        ok=token in out and 'init check memory pass.' in out and 'kernel panic' not in out and 'user panic' not in out
        if name=='forktree':ok=ok and len(re.findall(r"I am '[01]*'",out))==31
        results.append({'case':name,'memory_mib':mem,'passed':ok})
        print(f'[ABC] {name:22s} {mem:3d} MiB '+('PASS' if ok else 'FAIL'),flush=True)
        for line in out.splitlines():
            if line.startswith(('[C]','[COW]','[DirtyCOW model]','memory balance:','DIRTYCOW_MODEL','ABC_EXEC')):print('  '+line,flush=True)
        if not ok:
            print(out[-6000:]);raise RuntimeError(tag+' failed')
(LOG/'integration.json').write_text(json.dumps(results,indent=2))
print(f'ABC_INTEGRATION_ALL_PASS ({len(results)} cases)',flush=True)
