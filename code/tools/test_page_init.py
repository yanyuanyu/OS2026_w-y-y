"""Inject parsed memory values through real GDB; not a malformed-FDT test."""
from pathlib import Path
import socket, subprocess, tempfile, time, os, shutil, re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'evidence/page-init'
OUT.mkdir(parents=True, exist_ok=True)
prefix = os.environ.get('GCCPREFIX','riscv64-unknown-elf-')
gdb = os.environ.get('GDB') or shutil.which(prefix+'gdb') or shutil.which('gdb-multiarch')
assert gdb, 'RISC-V GDB is required'
subprocess.run(['make','-j2'],cwd=ROOT,check=True)
cases = [
    ('partial-tail','set variable memory_size=0x8000001',None),
    ('zero-base','set variable memory_base=0','invalid physical memory range'),
    ('zero-size','set variable memory_size=0','invalid physical memory range'),
    ('overflow','set variable memory_size=0xffffffffffffffff','invalid physical memory range'),
    ('outside-boot-map','set variable memory_size=0x40001000','outside boot mapping'),
    ('wrong-base','set variable memory_base=0x80001000','unsupported or unaligned'),
    ('no-free-pages','set variable memory_size=0x200000','consume physical memory'),
]
for name, change, expected in cases:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    serial=OUT/(name+'-serial.log')
    with tempfile.TemporaryDirectory(prefix='lab2-page-init-') as temp:
        script=Path(temp)/'case.gdb'
        script.write_text('set pagination off\nset confirm off\nfile bin/kernel\ntarget remote 127.0.0.1:'+str(port)+'\nhbreak pmm_init\ncontinue\n'+change+'\ndetach\nquit\n')
        child=subprocess.Popen([os.environ.get('QEMU','qemu-system-riscv64'),'-machine','virt','-nographic','-bios','default','-kernel','bin/ucore.img','-monitor','none','-serial','file:'+str(serial),'-S','-gdb','tcp:127.0.0.1:'+str(port)],cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            time.sleep(.3)
            result=subprocess.run([gdb,'-batch','-nx','-x',str(script)],cwd=ROOT,capture_output=True,text=True,timeout=15)
            (OUT/(name+'-gdb.log')).write_text(result.stdout+result.stderr)
            assert result.returncode==0,result.stdout+result.stderr
            deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                text=serial.read_text(errors='replace') if serial.exists() else ''
                # The panic heading precedes its reason; wait for the full
                # expected message before terminating the serial producer.
                if expected is None:
                    if 'check_alloc_page() succeeded!\n' in text:break
                elif re.search(re.escape(expected)+r'[^\n]*\n', text):
                    break
                time.sleep(.05)
        finally:
            child.terminate();child.wait(timeout=3)
    text=serial.read_text()
    if expected is None:
        assert 'check_alloc_page() succeeded!' in text and 'panic at' not in text,text
        assert 'Page descriptors: 32768 pages' in text and '0x0000000088000000)' in text,text
        print('PAGE_INIT partial-tail: complete pages only PASS',flush=True)
    else:
        assert 'kernel panic at' in text and expected in text,text
        assert 'check_alloc_page() succeeded!' not in text,text
        print('PAGE_INIT '+name+': expected protective panic PASS',flush=True)
