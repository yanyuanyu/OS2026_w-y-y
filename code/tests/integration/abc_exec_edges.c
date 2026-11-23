#include <stdio.h>
#include <ulib.h>
#include <unistd.h>
#include <elf.h>
#include <string.h>
#include <error.h>
#define UTEXT 0x800000UL
#define PGSIZE 4096UL
#define USTACKTOP 0x80000000UL
static unsigned char image[8193] __attribute__((aligned(4096)));
static int execute(void *data,size_t size) {
    register uintptr_t a0 asm("a0")=SYS_exec,a1 asm("a1")=(uintptr_t)"edges",a2 asm("a2")=5;
    register uintptr_t a3 asm("a3")=(uintptr_t)data,a4 asm("a4")=size;
    asm volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4):"memory");return a0;
}
static void prepare(int variant) {
    memset(image,0,sizeof(image));
    struct elfhdr *e=(void *)image;struct proghdr *p=(void *)(image+sizeof(*e));
    e->e_magic=ELF_MAGIC;e->e_phoff=sizeof(*e);e->e_phentsize=sizeof(*p);e->e_phnum=1;e->e_entry=UTEXT;
    p->p_type=ELF_PT_LOAD;p->p_flags=ELF_PF_R|ELF_PF_X;p->p_va=UTEXT;p->p_offset=256;p->p_filesz=12;p->p_memsz=12;
    unsigned int *ins=(void *)(image+256);ins[0]=0x00100513;ins[1]=0x04900593;ins[2]=0x00000073;
    if(variant==1)e->e_entry=UTEXT+PGSIZE;
    if(variant==2){e->e_phnum=2;p[1]=p[0];}
}
int main(void) {
    for(int v=0;v<3;v++) {
        prepare(v);int pid=fork();assert(pid>=0);
        if(pid==0){execute(image,sizeof(image));panic("exec returned");}
        int code=0;assert(waitpid(pid,&code)==0);
        if(v==0)assert(code==73);else assert(code<0);
    }
    int pid=fork();assert(pid>=0);
    if(pid==0){assert(execute((void *)(USTACKTOP-8*PGSIZE),64)==-E_INVAL);exit(0);}
    int code=0;assert(waitpid(pid,&code)==0 && code==0);
    cprintf("ABC_EXEC_EDGES_PASS: cross-page image, entry, overlap, unmapped buffer\n");return 0;
}
