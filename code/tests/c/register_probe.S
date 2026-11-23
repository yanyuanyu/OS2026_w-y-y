.text
.globl c_register_probe
c_register_probe:
    addi sp,sp,-32
    sd s2,0(sp)
    sd s3,8(sp)
    sd ra,16(sp)
    li s2,0x12345
    li s3,0x6789a
    li t0,0x111
    li t1,0x222
    li t2,0x333
    li a1,0x444
    li a5,0x555
    li a0,18
    ecall
    blez a0,1f
    li t3,0x12345
    bne s2,t3,1f
    li t3,0x6789a
    bne s3,t3,1f
    li t3,0x111
    bne t0,t3,1f
    li t3,0x222
    bne t1,t3,1f
    li t3,0x333
    bne t2,t3,1f
    li t3,0x444
    bne a1,t3,1f
    li t3,0x555
    bne a5,t3,1f
    li a0,0
    j 2f
1:  li a0,-1
2:  ld s2,0(sp)
    ld s3,8(sp)
    ld ra,16(sp)
    addi sp,sp,32
    ret
