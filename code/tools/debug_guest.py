"""Verify actual Sv39 root leaf and two store instructions; no GDB Python needed."""
from pathlib import Path
import subprocess, socket, time, re, os, shutil

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'evidence/guest-gdb'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    prefix = os.environ.get('GCCPREFIX', 'riscv64-unknown-elf-')
    gdb = os.environ.get('GDB') or shutil.which(prefix+'gdb') or shutil.which('gdb-multiarch')
    if not gdb:
        raise SystemExit('Set GDB or put RISC-V GDB on PATH')
    subprocess.run(['make', '-j2', 'PMM_ALGO=buddy'], cwd=ROOT, check=True)
    dis = subprocess.check_output([prefix+'objdump', '-d', '--disassemble=c_memory_probe', 'bin/buddy/kernel'], cwd=ROOT, text=True)
    (OUT/'probe-disassembly.log').write_text(dis)
    stores = [int(m.group(1), 16) for m in re.finditer(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(?:sd|c.sd|c.sdsp)\s', dis, re.M)]
    assert len(stores) == 2, dis
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    script = '''set pagination off
set confirm off
file bin/buddy/kernel
target remote 127.0.0.1:PORT
hbreak kern_init
continue
set $sv = (unsigned long long)$satp
set $root = ($sv & 0xfffffffffff) << 12
set $va = (unsigned long long)&kern_init
set $offset = (unsigned long long)0xffffffff40000000
set $index = ($va >> 30) & 511
set $pte = *(unsigned long long *)($root + $offset + $index * 8)
printf "GDB satp=%#lx root=%#lx VA=%#lx index=%lu PTE=%#lx\\n", $sv, $root, $va, $index, $pte
if ($sv >> 60) != 8 || $index != 511 || $pte != 0x200000cf
  quit 1
end
set $pa = (($pte >> 10) << 12) | ($va & 0x3fffffff)
if $pa != ($va - $offset)
  quit 1
end
printf "GDB SV39 WALK PASS: PA=%#lx leaf_size=0x40000000\\n", $pa
hbreak c_memory_probe
continue
set $probe = (unsigned long long)&c_probe_word
'''
    for address, value in zip(stores, (0x1122334455667788, 0x8877665544332211)):
        script += f'hbreak *{address:#x}\ncontinue\nx/i $pc\nsi\nset $value = *(unsigned long long *)$probe\nprintf "GDB STORE VA=%#lx PA=%#lx VALUE=%#lx\\n", $probe, $probe-$offset, $value\nif $value != {value:#x}\n  quit 1\nend\n'
    script += 'printf "GDB TWO STORE STEPS PASS\\n"\ndetach\nquit\n'
    path = OUT/'walk.gdb'
    path.write_text(script.replace('PORT', str(port)))
    with (OUT/'serial.log').open('w') as serial:
        proc = subprocess.Popen([os.environ.get('QEMU','qemu-system-riscv64'), '-machine', 'virt', '-m', '128', '-nographic', '-monitor', 'none', '-bios', 'default', '-kernel', 'bin/buddy/ucore.img', '-S', '-gdb', f'tcp:127.0.0.1:{port}'], cwd=ROOT, stdout=serial, stderr=subprocess.STDOUT)
        try:
            for _ in range(50):
                if proc.poll() is not None:
                    raise RuntimeError('QEMU exited before GDB attachment')
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.1)
            result = subprocess.run([gdb, '-batch', '-nx', '-x', str(path)], cwd=ROOT, text=True, capture_output=True, timeout=40)
            output = result.stdout+result.stderr
            (OUT/'walk.log').write_text(output)
            print(output, flush=True)
            assert result.returncode == 0 and 'GDB TWO STORE STEPS PASS' in output
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait()

if __name__ == '__main__':
    main()
