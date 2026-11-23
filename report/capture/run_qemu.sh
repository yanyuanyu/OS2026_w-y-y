#!/bin/bash
set -o pipefail
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:/usr/bin:/bin
cd "$(dirname "$0")/../../code" || exit 1
make -j2 build-hello || exit $?
python3 - <<'INNER'
import subprocess,os,select,time
p=subprocess.Popen(['qemu-system-riscv64','-machine','virt','-m','128','-nographic','-bios','default','-kernel','bin/ucore.img'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
data=b'';end=time.monotonic()+12
while time.monotonic()<end:
 r,_,_=select.select([p.stdout],[],[],0.3)
 if r:
  b=os.read(p.stdout.fileno(),4096)
  if not b:break
  data+=b
  if b'init check memory pass.' in data:break
p.terminate();tail,_=p.communicate(timeout=3);data+=tail
text=data.decode(errors='replace');start=text.find('memory management:')
print(text[start:] if start>=0 else text)
assert 'hello pass.' in text and 'init check memory pass.' in text
print('ABC_HELLO_RUN_PASS')
INNER
