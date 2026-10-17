#!/usr/bin/env python3
"""Host GDB of QEMU 4.1.1: real Sv39 store, soft TLB miss/hit, and Bare."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OFFSET = 0xffffffff40000000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()
    out = ROOT / "evidence" / "host-qemu"
    out.mkdir(parents=True, exist_ok=True)
    if args.binary:
        binary = args.binary.resolve()
    else:
        binary = Path(json.loads((out / "build.json").read_text())["binary"])
    version = subprocess.check_output([str(binary), "--version"], text=True)
    if "version 4.1.1" not in version:
        raise SystemExit("This source-line script requires QEMU 4.1.1")
    subprocess.run(["make", "-j2", "PMM_ALGO=buddy"], cwd=ROOT, check=True)
    nm = subprocess.check_output(["riscv64-unknown-elf-nm", "bin/buddy/kernel"], cwd=ROOT, text=True)
    def symbol(name):
        return int(re.search(rf"(?m)^([0-9a-f]+) \w {name}$", nm).group(1), 16)
    target = symbol("c_probe_word")
    hartid_pa = symbol("boot_hartid") - OFFSET
    common = """set pagination off
set confirm off
set breakpoint pending off
handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
set print pretty on
"""
    # BSS zeroing also writes this address, but byte-wise. Select the 8-byte
    # probe store at the caller where access size is available.
    sv39 = common + f"""break riscv_cpu_tlb_fill if address == {target:#x} && size == 8 && access_type == 1
run
tbreak get_physical_address
continue
printf "C-HOST SV39 cold store addr=%#lx satp=%#lx mode=%d\\n", addr, env->satp, mmu_idx
bt 8
set $physical_slot = physical
set $prot_slot = prot
set $walks = 1
commands 1
silent
set $walks = $walks + 1
continue
end
tbreak cpu_helper.c:254
continue
printf "C-HOST WALK i=%d levels=%d idx=%#lx base=%#lx pte_addr=%#lx pte=%#lx ptshift=%d\\n", i, levels, idx, base, pte_addr, pte, ptshift
python
import gdb
frame = gdb.newest_frame()
while frame and frame.name() != 'store_helper':
    frame = frame.older()
assert frame is not None
cold_tag = int(frame.read_var('tlb_addr')) & ((1 << 64) - 1)
print('C-HOST cold C store_helper write_tag=%#x (miss)' % cold_tag)
assert cold_tag != {target:#x}
assert int(gdb.parse_and_eval('i')) == 0
assert int(gdb.parse_and_eval('idx')) == 511
assert int(gdb.parse_and_eval('pte')) == 0x200000cf
end
finish
printf "C-HOST translated page=%#lx prot=%d\\n", *$physical_slot, *$prot_slot
python
assert int(gdb.parse_and_eval('*$physical_slot')) == {target - OFFSET:#x}
end
tbreak cpu_helper.c:474
continue
set $tlb = &cpu->neg.tlb
set $index = ({target:#x} >> 12) & ($tlb->f[mmu_idx].mask / sizeof(CPUTLBEntry))
set $entry = &$tlb->f[mmu_idx].table[$index]
printf "C-HOST TLB index=%lu write_tag=%#lx addend=%#lx\\n", $index, $entry->addr_write, $entry->addend
python
assert int(gdb.parse_and_eval('$entry->addr_write')) == {target:#x}
end
set $hostaddr = {target:#x} + $entry->addend
watch -location *(unsigned long long *)$hostaddr
continue
printf "C-HOST FIRST store value=%#lx walks=%d\\n", *(unsigned long long *)$hostaddr, $walks
bt 7
python
assert int(gdb.parse_and_eval('*(unsigned long long *)$hostaddr')) == 0x1122334455667788
end
continue
printf "C-HOST SECOND store value=%#lx walks=%d\\n", *(unsigned long long *)$hostaddr, $walks
bt 7
python
assert int(gdb.parse_and_eval('*(unsigned long long *)$hostaddr')) == 0x8877665544332211
assert int(gdb.parse_and_eval('$walks')) == 1
end
printf "C-HOST SV39 store + TLB cold/hot PASS\\n"
kill
quit
"""
    bare = common + f"""break get_physical_address if addr == {hartid_pa:#x} && access_type == 1 && mmu_idx == 1
run
printf "C-HOST BARE addr=%#lx satp=%#lx mode=%d\\n", addr, env->satp, mmu_idx
bt 8
set $physical_slot = physical
set $prot_slot = prot
python
import gdb
assert int(gdb.parse_and_eval('env->satp')) == 0
end
finish
printf "C-HOST BARE physical=%#lx prot=%d\\n", *$physical_slot, *$prot_slot
python
assert int(gdb.parse_and_eval('*$physical_slot')) == {hartid_pa:#x}
end
tbreak cpu_helper.c:474
continue
printf "C-HOST BARE still fills software TLB PASS\\n"
kill
quit
"""
    for name, script, marker in (("sv39-store-tlb", sv39, "C-HOST SV39 store + TLB cold/hot PASS"),
                                 ("bare", bare, "C-HOST BARE still fills software TLB PASS")):
        path = out / f"{name}.gdb"
        path.write_text(script)
        command = ["gdb", "-q", "-nx", "-batch", "-x", str(path), "--args", str(binary),
                   "-machine", "virt", "-smp", "1", "-m", "128", "-nographic",
                   "-monitor", "none", "-bios", "default", "-kernel", "bin/buddy/ucore.img"]
        proc = subprocess.Popen(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            text, _ = proc.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGKILL)
            text, _ = proc.communicate()
        finally:
            # GDB and its inferior share this newly-created private group.
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        (out / f"{name}.log").write_text(text)
        print(text, end="", flush=True)
        if timed_out or proc.returncode or marker not in text:
            raise RuntimeError(f"host debugger verification failed: {name}")


if __name__ == "__main__":
    main()
