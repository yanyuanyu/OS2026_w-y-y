#include <defs.h>
#include <unistd.h>
#include <syscall.h>

//lab5 C 2413682：固定参数个数；寄存器操作数让编译器理解 ecall 的输入输出。
static inline int syscall(uint64_t num,uint64_t p1,uint64_t p2,uint64_t p3,uint64_t p4,uint64_t p5) {
    register uintptr_t a0 asm("a0")=num;
    register uintptr_t a1 asm("a1")=p1;
    register uintptr_t a2 asm("a2")=p2;
    register uintptr_t a3 asm("a3")=p3;
    register uintptr_t a4 asm("a4")=p4;
    register uintptr_t a5 asm("a5")=p5;
    asm volatile("ecall" : "+r"(a0) : "r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5) : "memory");
    return (int)a0;
}
int sys_exit(int64_t code){return syscall(SYS_exit,code,0,0,0,0);}
int sys_fork(void){return syscall(SYS_fork,0,0,0,0,0);}
int sys_wait(int64_t pid,int *store){return syscall(SYS_wait,pid,(uintptr_t)store,0,0,0);}
int sys_yield(void){return syscall(SYS_yield,0,0,0,0,0);}
int sys_kill(int64_t pid){return syscall(SYS_kill,pid,0,0,0,0);}
int sys_getpid(void){return syscall(SYS_getpid,0,0,0,0,0);}
int sys_putc(int64_t c){return syscall(SYS_putc,c,0,0,0,0);}
int sys_pgdir(void){return syscall(SYS_pgdir,0,0,0,0,0);}
