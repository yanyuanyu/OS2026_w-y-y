set pagination off
set confirm off
set breakpoint pending off
handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
set print pretty on
break get_physical_address if addr == 0x80206000 && access_type == 1 && mmu_idx == 1
run
printf "C-HOST BARE addr=%#lx satp=%#lx mode=%d\n", addr, env->satp, mmu_idx
bt 8
set $physical_slot = physical
set $prot_slot = prot
python
import gdb
assert int(gdb.parse_and_eval('env->satp')) == 0
end
finish
printf "C-HOST BARE physical=%#lx prot=%d\n", *$physical_slot, *$prot_slot
python
assert int(gdb.parse_and_eval('*$physical_slot')) == 0x80206000
end
tbreak cpu_helper.c:474
continue
printf "C-HOST BARE still fills software TLB PASS\n"
kill
quit
