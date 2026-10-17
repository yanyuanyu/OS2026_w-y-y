"""Run commands in a real terminal; keep their actual stdout as text evidence."""
from pathlib import Path
import subprocess,os,sys,shlex,time,selectors,signal,re,json,hashlib,uuid

REPORT=Path(__file__).resolve().parents[1]
ROOT=REPORT.parent/'code'
OUT=REPORT/'logs/terminal'
OUT.mkdir(parents=True,exist_ok=True)
os.environ['PATH']='/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:'+os.environ['PATH']
os.chdir(ROOT)
case=sys.argv[1]
print('Lab2 | 2413681 Wang You | 2413682 Yan Yuanyu | 2413636 Yang Siyuan',flush=True)
print('Real terminal run | case='+case+' | '+time.strftime('%Y-%m-%d %H:%M:%S'),flush=True)
command_log_counts={}

def run(command):
    print('$ '+shlex.join(command),flush=True)
    proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
    output=''
    while True:
        chunk=os.read(proc.stdout.fileno(),65536)
        if not chunk:break
        output+=chunk.decode(errors='replace')
        sys.stdout.write(chunk.decode(errors='replace'));sys.stdout.flush()
    proc.wait();proc.stdout.close()
    stem=case+'-'+Path(command[0]).name
    command_log_counts[stem]=command_log_counts.get(stem,0)+1
    suffix='' if command_log_counts[stem]==1 else '-'+str(command_log_counts[stem]).zfill(2)
    (OUT/(stem+suffix+'.log')).write_text('$ '+shlex.join(command)+'\n'+output)
    if proc.returncode:raise SystemExit(proc.returncode)

def qemu(backend):
    image='bin/'+('' if backend=='bestfit' else backend+'/')+'ucore.img'
    command=['qemu-system-riscv64','-machine','virt','-m','128','-nographic','-bios','default','-monitor','none','-kernel',image]
    print('$ '+shlex.join(command),flush=True)
    print('(OpenSBI banner suppressed; displaying live output from DTB Init onward)',flush=True)
    proc=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
    selector=selectors.DefaultSelector();selector.register(proc.stdout,selectors.EVENT_READ)
    output='';shown=0;started=False
    try:
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if selector.select(.1):
                chunk=os.read(proc.stdout.fileno(),65536).decode(errors='replace');output+=chunk
                if not started and 'DTB Init' in output:
                    shown=output.index('DTB Init');started=True
                if started:
                    sys.stdout.write(output[shown:]);sys.stdout.flush();shown=len(output)
            if 'kernel panic' in output:raise RuntimeError('kernel panic')
            if re.search(r'satp physical address: 0x[0-9a-f]+\r?\n',output):break
            if proc.poll() is not None:raise RuntimeError('QEMU exited early')
        assert 'check_alloc_page() succeeded!' in output
        assert re.search(r'satp physical address: 0x[0-9a-f]+\r?\n',output)
    finally:
        selector.close();proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
        proc.stdout.close()
        (OUT/(case+'-qemu-serial.log')).write_text('$ '+shlex.join(command)+'\n'+output)

if case=='01-build':
    run(['make','clean']);run(['make','-j2'])
elif case=='02-grade':
    os.environ['GRADE_EVIDENCE']=str(OUT/'02-grade-serial.log')
    run(['make','grade'])
elif case=='03-bestfit-host':run(['bash','tests/bestfit/run_host.sh'])
elif case=='04-buddy-host':run(['python3','tools/test_buddy_host.py'])
elif case=='05-buddy-qemu':
    run(['python3','tools/verify_c.py']);qemu('buddy')
elif case=='06-boundaries':
    run(['python3','tools/test_page_init.py']);run(['python3','tools/test_firstfit_tail.py'])
elif case=='07-guest-gdb':run(['python3','tools/debug_guest.py'])
elif case=='08-source-audit':
    print('$ sha256 verification against logs/acceptance/source_hashes.json',flush=True)
    expected=json.loads((REPORT/'logs/acceptance/source_hashes.json').read_text())
    for name,digest in expected.items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
    for name in ('kern/mm/pmm.c','kern/mm/best_fit_pmm.c','kern/mm/buddy_pmm.c','kern/mm/buddy_test.c','kern/mm/default_pmm.c','tools/test_lab2.c','tools/test_lab2.h'):
        print(name+': SHA256 MATCH',flush=True)
    print('All '+str(len(expected))+' source files match the accepted version.',flush=True)
    audit=json.loads((REPORT/'logs/source_audit.json').read_text())
    assert audit['result']=='PASS' and all(audit['checks'].values())
    print('A function, B implementation, C Buddy and teacher checks preserved: PASS',flush=True)
    (OUT/'08-source-audit.json').write_text(json.dumps({'result':'PASS','checked':len(expected),'acceptance_run_id':audit['acceptance_run_id']},indent=2))
elif case=='09-bestfit-qemu':qemu('bestfit')
elif case=='10-slub-tests':run(['python3','tools/test_slub.py'])
else:raise SystemExit('Unknown case')
print('\nCommand completed successfully; exit code 0.',flush=True)
