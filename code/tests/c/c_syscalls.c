#include <ulib.h>
#include <stdio.h>
#include <error.h>
#include <unistd.h>

extern int c_register_probe(void);
static int raw(uint64_t n) {
    register uintptr_t a0 asm("a0")=n;
    asm volatile("ecall" : "+r"(a0) :: "memory");
    return (int)a0;
}
int main(void) {
    int pid=getpid();assert(pid>1);
    assert(raw(0)==-E_INVAL);
    assert(raw(SYS_clone)==-E_INVAL);
    assert(raw(0xffffffffffffffffULL)==-E_INVAL);
    assert(raw((1ULL<<32)|SYS_getpid)==-E_INVAL);
    cprintf("[C] invalid/sparse/64-bit syscall numbers PASS\n");
    assert(c_register_probe()==0);
    for(int i=0;i<1000;i++){assert(getpid()==pid);yield();}
    cprintf("[C] register restoration and 1000 ecall/yield rounds PASS\n");
    assert(kill(-1)==-E_INVAL);
    assert(kill(1)==-E_INVAL);
    int child=fork();assert(child>=0);
    if(child==0){while(1)yield();}
    assert(kill(child)==0);
    int code=0;assert(waitpid(child,&code)==0);assert(code==-E_KILLED);
    assert(waitpid(child,NULL)==-E_BAD_PROC);
    cprintf("[C] kill, interrupted wait and child collection PASS\n");
    cprintf("C_SYSCALLS_ALL_PASS\n");
    return 0;
}
