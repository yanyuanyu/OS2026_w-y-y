#!/usr/bin/env python3
"""Two genuine GDB sessions: guest RISC-V + host QEMU, using stdlib pipes."""
from pathlib import Path
import os,subprocess,select,time,signal,re,json,tempfile,shutil
ROOT=Path(__file__).resolve().parents[2]
LOG=ROOT.parent/'report/logs';LOG.mkdir(parents=True,exist_ok=True)
ENV=os.environ.copy();ENV['PATH']='/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin'
class Debugger:
    def __init__(self,exe,args,cwd,log):
        self.f=open(log,'w');self.buf=b''
        self.p=subprocess.Popen([exe,'-nx','-q',*args],cwd=cwd,env=ENV,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
        self.wait()
    def wait(self,timeout=25):
        end=time.monotonic()+timeout
        while b'(gdb) ' not in self.buf:
            left=end-time.monotonic()
            if left<=0:raise TimeoutError(self.buf.decode(errors='replace'))
            rd,_,_=select.select([self.p.stdout],[],[],min(left,1))
            if rd:
                data=os.read(self.p.stdout.fileno(),65536)
                if not data:raise RuntimeError('GDB exited: '+self.buf.decode(errors='replace'))
                self.f.write(data.decode(errors='replace'));self.f.flush();self.buf+=data
        out,self.buf=self.buf.split(b'(gdb) ',1)
        return out.decode(errors='replace')
    def send(self,cmd):
        self.f.write('\n>>> '+cmd+'\n');self.f.flush();self.p.stdin.write((cmd+'\n').encode());self.p.stdin.flush()
    def cmd(self,cmd,timeout=25):
        self.send(cmd);return self.wait(timeout)
    def stop(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:self.p.wait(timeout=3)
            except subprocess.TimeoutExpired:self.p.kill();self.p.wait()
        self.f.close()

with tempfile.TemporaryDirectory(prefix='lab5-c-dual-') as tmp:
    dst=Path(tmp)/'code';shutil.copytree(ROOT,dst,ignore=shutil.ignore_patterns('obj','bin','.git','__pycache__'))
    build=subprocess.run(['make','-j2','build-hello'],cwd=dst,env=ENV,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (LOG/'dual_build.log').write_text(build.stdout)
    assert build.returncode==0
    dis=subprocess.check_output(['riscv64-unknown-elf-objdump','-d','obj/__user_hello.out'],cwd=dst,env=ENV,text=True)
    part=dis.split('<sys_getpid>:',1)[1].split('\n\n',1)[0]
    ecall=int(re.search(r'^\s*([0-9a-f]+):.*\becall\b',part,re.M)[1],16)
    dis=subprocess.check_output(['riscv64-unknown-elf-objdump','-d','bin/kernel'],cwd=dst,env=ENV,text=True)
    sret=int(re.search(r'^\s*([0-9a-f]+):.*\bsret\b',dis,re.M)[1],16)
    host=guest=None
    try:
        host=Debugger('gdb',['/opt/qemu/bin/qemu-system-riscv64'],dst,LOG/'dual_host_gdb.log')
        for c in ['set pagination off','set confirm off','set print pretty on','set debuginfod enabled off']:
            host.cmd(c)
        host.cmd('set args -machine virt -m 128 -bios default -kernel bin/ucore.img -display none -serial file:'+str(LOG/'dual_qemu.log')+' -monitor none -S -gdb tcp:127.0.0.1:1248')
        host.send('run');time.sleep(1)
        guest=Debugger('riscv64-unknown-elf-gdb',['bin/kernel'],dst,LOG/'dual_guest_gdb.log')
        for c in ['set pagination off','set confirm off','set remotetimeout 20','target remote 127.0.0.1:1248','add-symbol-file obj/__user_hello.out',f'hbreak *0x{ecall:x}']:
            guest.cmd(c)
        before=guest.cmd('continue')
        assert 'Breakpoint' in before,before
        guest.cmd('x/i $pc');guest.cmd('info registers pc sp a0 a1 sstatus sepc scause')
        os.kill(host.p.pid,signal.SIGINT);host.wait()
        bp=host.cmd('break helper_raise_exception if exception == 8');assert 'Breakpoint' in bp,bp
        host.send('continue');guest.send('stepi')
        hit=host.wait();assert 'helper_raise_exception' in hit,hit
        host.cmd('set $saved_env = env')
        host.cmd('printf "ECALL before: priv=%ld pc=%#lx a0=%ld\\n", env->priv, env->pc, env->gpr[10]')
        host.cmd('bt 5');host.cmd('disable 1')
        host.cmd('break riscv_cpu_do_interrupt if cs->exception_index == 8')
        host.send('continue');host.wait()
        host.cmd('bt 4')
        host.cmd('finish')
        after=host.cmd('printf "ECALL after: priv=%ld pc=%#lx sepc=%#lx scause=%ld\\n", $saved_env->priv, $saved_env->pc, $saved_env->sepc, $saved_env->scause')
        assert 'priv=1' in after and 'scause=8' in after,after
        host.cmd('disable 2');host.send('continue')
        guest.wait();guest.cmd('info registers pc sstatus sepc scause')
        guest.cmd(f'hbreak *0x{sret:x}')
        guest.cmd('continue');guest.cmd('x/i $pc');guest.cmd('info registers pc sp a0 sstatus sepc')
        os.kill(host.p.pid,signal.SIGINT);host.wait()
        host.cmd('break helper_sret');host.send('continue');guest.send('stepi')
        hit=host.wait();assert 'helper_sret' in hit,hit
        host.cmd('set $saved_env = env')
        host.cmd('printf "SRET before: priv=%ld pc=%#lx sepc=%#lx\\n", env->priv,env->pc,env->sepc')
        host.cmd('bt 5');host.cmd('finish')
        after=host.cmd('printf "SRET after: priv=%ld return_pc=%#lx\\n", $saved_env->priv, $saved_env->sepc')
        assert 'priv=0' in after,after
        host.cmd('disable 3');host.send('continue')
        guest.wait();final=guest.cmd('info registers pc sp a0 sstatus sepc')
        expected=ecall+4
        pc=guest.cmd('p/x $pc');assert hex(expected) in pc,pc
        ret=guest.cmd('p/d $a0');assert '= 2' in ret,ret
        guest.cmd('detach');guest.send('quit');guest.p.wait(timeout=3)
        os.kill(host.p.pid,signal.SIGINT);host.wait();host.cmd('kill');host.send('quit');host.p.wait(timeout=3)
        result={'ecall_pc':hex(ecall),'sret_pc':hex(sret),'returned_pc':hex(expected),'returned_pid':2,'ecall_privilege':'U -> S','sret_privilege':'S -> U','passed':True}
        (LOG/'dual_gdb.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2));print('DUAL_GDB_ALL_PASS',flush=True)
    finally:
        if guest:guest.stop()
        if host:host.stop()
