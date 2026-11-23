/* 测试专用 S-mode trap 桥，仅验证真实写缺页重试；不是 C 模块交付。 */
    .section .text
    .align 2
    .globl a_store_trap
a_store_trap:
    addi sp, sp, -256
    .irp n,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, \n*8(sp)
    .endr
    csrr a0, scause
    csrr a1, stval
    call a_handle_store_fault
    .irp n,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld x\n, \n*8(sp)
    .endr
    addi sp, sp, 256
    sret
