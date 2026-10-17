set pagination off
set confirm off
set breakpoint pending off
handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
set print pretty on
break riscv_cpu_tlb_fill if address == 0xffffffffc0208000 && size == 8 && access_type == 1
run
tbreak get_physical_address
continue
printf "C-HOST SV39 cold store addr=%#lx satp=%#lx mode=%d\n", addr, env->satp, mmu_idx
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
printf "C-HOST WALK i=%d levels=%d idx=%#lx base=%#lx pte_addr=%#lx pte=%#lx ptshift=%d\n", i, levels, idx, base, pte_addr, pte, ptshift
python
import gdb
frame = gdb.newest_frame()
while frame and frame.name() != 'store_helper':
    frame = frame.older()
assert frame is not None
cold_tag = int(frame.read_var('tlb_addr')) & ((1 << 64) - 1)
print('C-HOST cold C store_helper write_tag=%#x (miss)' % cold_tag)
assert cold_tag != 0xffffffffc0208000
assert int(gdb.parse_and_eval('i')) == 0
assert int(gdb.parse_and_eval('idx')) == 511
assert int(gdb.parse_and_eval('pte')) == 0x200000cf
end
finish
printf "C-HOST translated page=%#lx prot=%d\n", *$physical_slot, *$prot_slot
python
assert int(gdb.parse_and_eval('*$physical_slot')) == 0x80208000
end
tbreak cpu_helper.c:474
continue
set $tlb = &cpu->neg.tlb
set $index = (0xffffffffc0208000 >> 12) & ($tlb->f[mmu_idx].mask / sizeof(CPUTLBEntry))
set $entry = &$tlb->f[mmu_idx].table[$index]
printf "C-HOST TLB index=%lu write_tag=%#lx addend=%#lx\n", $index, $entry->addr_write, $entry->addend
python
assert int(gdb.parse_and_eval('$entry->addr_write')) == 0xffffffffc0208000
end
set $hostaddr = 0xffffffffc0208000 + $entry->addend
watch -location *(unsigned long long *)$hostaddr
continue
printf "C-HOST FIRST store value=%#lx walks=%d\n", *(unsigned long long *)$hostaddr, $walks
bt 7
python
assert int(gdb.parse_and_eval('*(unsigned long long *)$hostaddr')) == 0x1122334455667788
end
continue
printf "C-HOST SECOND store value=%#lx walks=%d\n", *(unsigned long long *)$hostaddr, $walks
bt 7
python
assert int(gdb.parse_and_eval('*(unsigned long long *)$hostaddr')) == 0x8877665544332211
assert int(gdb.parse_and_eval('$walks')) == 1
end
printf "C-HOST SV39 store + TLB cold/hot PASS\n"
kill
quit
