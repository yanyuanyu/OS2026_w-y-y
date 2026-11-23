#include <ulib.h>
#include <stdio.h>
#include <error.h>

static volatile int shared=7;
static void readonly_target(void) { asm volatile(""); }
int main(void) {
    int p=fork();assert(p>=0);
    if(p==0){assert(shared==7);shared=42;assert(shared==42);exit(23);}
    shared=19;
    int status=0;assert(waitpid(p,&status)==0 && status==23);assert(shared==19);
    cprintf("[COW] user store fault retried, parent/child isolation PASS\n");
    p=fork();assert(p>=0);
    if(p==0){*(volatile unsigned char *)(uintptr_t)readonly_target=0;exit(99);}
    assert(waitpid(p,&status)==0 && status==-E_KILLED);
    cprintf("[COW] read-only text write rejected, parent survives PASS\n");
    p=fork();assert(p>=0);
    if(p==0){*(volatile unsigned int *)0x1000=3;exit(99);}
    assert(waitpid(p,&status)==0 && status==-E_KILLED);
    cprintf("[COW] unmapped write rejected PASS\n");
    cprintf("C_COW_ALL_PASS\n");return 0;
}
