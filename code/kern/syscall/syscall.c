#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>

//lab5 function1 2413682：本层只转换参数，进程资源操作交给 A/B 的 do_*。
static int sys_exit(uint64_t a[]) { return do_exit((int)a[0]); }
static int sys_fork(uint64_t a[]) {
    return do_fork(0, current->tf->gpr.sp, current->tf);
}
static int sys_wait(uint64_t a[]) { return do_wait((int)a[0], (int *)a[1]); }
static int sys_exec(uint64_t a[]) {
    return do_execve((const char *)a[0], (size_t)a[1],
                     (unsigned char *)a[2], (size_t)a[3]);
}
static int sys_yield(uint64_t a[]) { return do_yield(); }
static int sys_kill(uint64_t a[]) { return do_kill((int)a[0]); }
static int sys_getpid(uint64_t a[]) { return current->pid; }
static int sys_putc(uint64_t a[]) { cputchar((int)a[0]); return 0; }
static int sys_pgdir(uint64_t a[]) { print_pgdir(); return 0; }

static int (*const syscalls[])(uint64_t[]) = {
    [SYS_exit]=sys_exit, [SYS_fork]=sys_fork, [SYS_wait]=sys_wait,
    [SYS_exec]=sys_exec, [SYS_yield]=sys_yield, [SYS_kill]=sys_kill,
    [SYS_getpid]=sys_getpid, [SYS_putc]=sys_putc, [SYS_pgdir]=sys_pgdir,
};
#define NUM_SYSCALLS (sizeof(syscalls)/sizeof(syscalls[0]))

void syscall(void) {
    assert(current != NULL && current->tf != NULL);
    struct trapframe *tf=current->tf;
    uint64_t num=tf->gpr.a0;
    uint64_t args[]={tf->gpr.a1,tf->gpr.a2,tf->gpr.a3,tf->gpr.a4,tf->gpr.a5};
    // 先检查完整 64 位调用号，再访问稀疏表；空槽和越界都返回错误。
    int ret=-E_INVAL;
    if (num < NUM_SYSCALLS && syscalls[num] != NULL) ret=syscalls[num](args);
    tf->gpr.a0=(intptr_t)ret;
}
