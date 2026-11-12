#include <riscv.h>

    .macro SAVE_ALL
    .align 2

    csrw sscratch, sp

    addi sp, sp, -36 * REGBYTES
    # save gprs
    STORE  x1,1*REGBYTES(sp)
    STORE  x3,3*REGBYTES(sp)
    STORE  x4,4*REGBYTES(sp)
    STORE  x5,5*REGBYTES(sp)
    STORE  x6,6*REGBYTES(sp)
    STORE  x7,7*REGBYTES(sp)
    STORE  x8,8*REGBYTES(sp)
    STORE  x9,9*REGBYTES(sp)
    STORE  x10,10*REGBYTES(sp)
    STORE  x11,11*REGBYTES(sp)
    STORE  x12,12*REGBYTES(sp)
    STORE  x13,13*REGBYTES(sp)
    STORE  x14,14*REGBYTES(sp)
    STORE  x15,15*REGBYTES(sp)
    STORE  x16,16*REGBYTES(sp)
    STORE  x17,17*REGBYTES(sp)
    STORE  x18,18*REGBYTES(sp)
    STORE  x19,19*REGBYTES(sp)
    STORE  x20,20*REGBYTES(sp)
    STORE  x21,21*REGBYTES(sp)
    STORE  x22,22*REGBYTES(sp)
    STORE  x23,23*REGBYTES(sp)
    STORE  x24,24*REGBYTES(sp)
    STORE  x25,25*REGBYTES(sp)
    STORE  x26,26*REGBYTES(sp)
    STORE  x27,27*REGBYTES(sp)
    STORE  x28,28*REGBYTES(sp)
    STORE  x29,29*REGBYTES(sp)
    STORE  x30,30*REGBYTES(sp)
    STORE  x31,31*REGBYTES(sp)

    # get sr, epc, badvaddr, cause
    csrr s0, sscratch
    csrr s1, sstatus
    csrr s2, sepc
    csrr s3, 0x143
    csrr s4, scause

    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

    .macro RESTORE_ALL
    #lab3 迁移依赖


    #lab3 function1 2413682
    # Return CSRs first; temporary GPRs are restored below.
    /* 【恢复1】恢复两个返回所需的 CSR
     * 先借 s1/s2 搬运两个返回所需的 CSR。
     * 从槽32/33加载到普通寄存器，再 csrw 写入 sstatus/sepc。
     * s1/s2 此时是临时载体，下面还会从第9/18槽恢复其原值。
     * 不要先恢复 s1/s2 再借用，否则会再次破坏恢复好的现场。
     * 保存的 SPP 决定返回权限，不能为了“返回”而一律把它清成0。 
     * 为什么借用 s1、s2？因为不能直接把内存中的值写入 CSR，需要通用寄存器中转：
     * 栈帧 → 通用寄存器 → CSR */
    /* STORE：寄存器 → 内存
     * LOAD： 内存 → 寄存器*/

    LOAD s1, 32*REGBYTES(sp)
    LOAD s2, 33*REGBYTES(sp)
    csrw sstatus, s1
    csrw sepc, s2
    /* 【恢复2】恢复旧 ra 和其他通用寄存器（取回它被打断前的值），暂不恢复 x2/sp。
     * x0 固定为0，无需加载。普通调用约定中的 caller-saved 寄存器也要恢复，
     * 因为被中断的程序没有主动发起普通函数调用，不能让它承担寄存器破坏。 */
    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    LOAD x4, 4*REGBYTES(sp)
    LOAD x5, 5*REGBYTES(sp)
    LOAD x6, 6*REGBYTES(sp)
    LOAD x7, 7*REGBYTES(sp)
    LOAD x8, 8*REGBYTES(sp)
    LOAD x9, 9*REGBYTES(sp)
    LOAD x10, 10*REGBYTES(sp)
    LOAD x11, 11*REGBYTES(sp)
    LOAD x12, 12*REGBYTES(sp)
    LOAD x13, 13*REGBYTES(sp)
    LOAD x14, 14*REGBYTES(sp)
    LOAD x15, 15*REGBYTES(sp)
    LOAD x16, 16*REGBYTES(sp)
    LOAD x17, 17*REGBYTES(sp)
    LOAD x18, 18*REGBYTES(sp)
    LOAD x19, 19*REGBYTES(sp)
    LOAD x20, 20*REGBYTES(sp)
    LOAD x21, 21*REGBYTES(sp)
    LOAD x22, 22*REGBYTES(sp)
    LOAD x23, 23*REGBYTES(sp)
    LOAD x24, 24*REGBYTES(sp)
    LOAD x25, 25*REGBYTES(sp)
    LOAD x26, 26*REGBYTES(sp)
    LOAD x27, 27*REGBYTES(sp)
    LOAD x28, 28*REGBYTES(sp)
    LOAD x29, 29*REGBYTES(sp)
    LOAD x30, 30*REGBYTES(sp)
    LOAD x31, 31*REGBYTES(sp)
    # Restore the pre-Trap sp last, retaining the frame base until now.
    /* 【答辩重点：为什么 sp 最后恢复】
     * 所有 LOAD 都用 sp 定位 frame。若先改 sp，后续 LOAD 会去旧栈顶附近读，
     * 而不是从这份 frame 读取。此条计算地址时仍使用当前 frame 基址，
     * 加载完成才把 x2/sp 改回第2槽保存的 old_sp。
     * 旧 sp 恢复后，这288字节在栈上被逻辑释放；不需要再额外 addi sp,288。 */
    LOAD x2, 2*REGBYTES(sp)

    
    .endm

    .globl __alltraps
__alltraps:
    SAVE_ALL

    #lab3 迁移依赖：传递当前中断帧给 C 分发函数。
    move a0, sp
    jal trap
    # sp should be the same as before "jal trap"

    .globl __trapret
__trapret:
    RESTORE_ALL
    # go back from supervisor call
    sret
 
    .globl forkrets
forkrets:
    # set stack to this new process's trapframe
    move sp, a0
    j __trapret
