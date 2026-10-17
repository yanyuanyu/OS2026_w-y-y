set pagination off
set confirm off
file bin/buddy/kernel
target remote 127.0.0.1:44106
hbreak kern_init
continue
set $sv = (unsigned long long)$satp
set $root = ($sv & 0xfffffffffff) << 12
set $va = (unsigned long long)&kern_init
set $offset = (unsigned long long)0xffffffff40000000
set $index = ($va >> 30) & 511
set $pte = *(unsigned long long *)($root + $offset + $index * 8)
printf "GDB satp=%#lx root=%#lx VA=%#lx index=%lu PTE=%#lx\n", $sv, $root, $va, $index, $pte
if ($sv >> 60) != 8 || $index != 511 || $pte != 0x200000cf
  quit 1
end
set $pa = (($pte >> 10) << 12) | ($va & 0x3fffffff)
if $pa != ($va - $offset)
  quit 1
end
printf "GDB SV39 WALK PASS: PA=%#lx leaf_size=0x40000000\n", $pa
hbreak c_memory_probe
continue
set $probe = (unsigned long long)&c_probe_word
hbreak *0xffffffffc020140a
continue
x/i $pc
si
set $value = *(unsigned long long *)$probe
printf "GDB STORE VA=%#lx PA=%#lx VALUE=%#lx\n", $probe, $probe-$offset, $value
if $value != 0x1122334455667788
  quit 1
end
hbreak *0xffffffffc0201412
continue
x/i $pc
si
set $value = *(unsigned long long *)$probe
printf "GDB STORE VA=%#lx PA=%#lx VALUE=%#lx\n", $probe, $probe-$offset, $value
if $value != 0x8877665544332211
  quit 1
end
printf "GDB TWO STORE STEPS PASS\n"
detach
quit
