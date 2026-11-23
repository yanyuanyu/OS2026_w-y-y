#include <stdio.h>
#include <ulib.h>
#include <unistd.h>
#include <elf.h>
#include <string.h>
static int exec_image(void *image, unsigned long size) {
    register unsigned long a0 asm("a0") = SYS_exec;
    register unsigned long a1 asm("a1") = (unsigned long)"new";
    register unsigned long a2 asm("a2") = 3;
    register unsigned long a3 asm("a3") = (unsigned long)image;
    register unsigned long a4 asm("a4") = size;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4) : "memory");
    return (int)a0;
}
int main(void) {
    int code = 0, pid = fork(); assert(pid >= 0);
    if (pid == 0) {
        unsigned char image[256]; memset(image, 0, sizeof(image));
        struct elfhdr *h = (struct elfhdr *)image;
        struct proghdr *p = (struct proghdr *)(image + sizeof(*h));
        h->e_magic = ELF_MAGIC; h->e_phoff = sizeof(*h); h->e_phentsize = sizeof(*p); h->e_phnum = 1; h->e_entry = 0x800000;
        p->p_type = ELF_PT_LOAD; p->p_flags = ELF_PF_R | ELF_PF_X; p->p_offset = 192; p->p_va = h->e_entry; p->p_filesz = 12; p->p_memsz = 12;
        unsigned int *insn = (unsigned int *)(image + 192); insn[0] = 0x00100513; insn[1] = 0x03700593; insn[2] = 0x00000073;
        exec_image(image, sizeof(image)); panic("exec returned");
    }
    assert(waitpid(pid, &code) == 0 && code == 55); cprintf("b_execsuccess pass.\n"); return 0;
}
