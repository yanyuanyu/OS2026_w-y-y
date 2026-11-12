#include <riscv.h>
.text
.align 2
.globl c_check_saved_registers
c_check_saved_registers:
    addi sp, sp, -112
    STORE ra, 0*REGBYTES(sp)
    STORE s0, 1*REGBYTES(sp)
    STORE s1, 2*REGBYTES(sp)
    STORE s2, 3*REGBYTES(sp)
    STORE s3, 4*REGBYTES(sp)
    STORE s4, 5*REGBYTES(sp)
    STORE s5, 6*REGBYTES(sp)
    STORE s6, 7*REGBYTES(sp)
    STORE s7, 8*REGBYTES(sp)
    STORE s8, 9*REGBYTES(sp)
    STORE s9, 10*REGBYTES(sp)
    STORE s10, 11*REGBYTES(sp)
    STORE s11, 12*REGBYTES(sp)
    li s0, 288
    li s1, 289
    li s2, 290
    li s3, 291
    li s4, 292
    li s5, 293
    li s6, 294
    li s7, 295
    li s8, 296
    li s9, 297
    li s10, 298
    li s11, 299
    call schedule
    li a0, 1
    li t0, 288
    bne s0, t0, 1f
    li t0, 289
    bne s1, t0, 1f
    li t0, 290
    bne s2, t0, 1f
    li t0, 291
    bne s3, t0, 1f
    li t0, 292
    bne s4, t0, 1f
    li t0, 293
    bne s5, t0, 1f
    li t0, 294
    bne s6, t0, 1f
    li t0, 295
    bne s7, t0, 1f
    li t0, 296
    bne s8, t0, 1f
    li t0, 297
    bne s9, t0, 1f
    li t0, 298
    bne s10, t0, 1f
    li t0, 299
    bne s11, t0, 1f
    j 2f
1:  li a0, 0
2:
    LOAD ra, 0*REGBYTES(sp)
    LOAD s0, 1*REGBYTES(sp)
    LOAD s1, 2*REGBYTES(sp)
    LOAD s2, 3*REGBYTES(sp)
    LOAD s3, 4*REGBYTES(sp)
    LOAD s4, 5*REGBYTES(sp)
    LOAD s5, 6*REGBYTES(sp)
    LOAD s6, 7*REGBYTES(sp)
    LOAD s7, 8*REGBYTES(sp)
    LOAD s8, 9*REGBYTES(sp)
    LOAD s9, 10*REGBYTES(sp)
    LOAD s10, 11*REGBYTES(sp)
    LOAD s11, 12*REGBYTES(sp)
    addi sp, sp, 112
    ret
