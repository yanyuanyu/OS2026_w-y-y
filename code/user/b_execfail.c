#include <stdio.h>
#include <ulib.h>
#include <unistd.h>
#include <error.h>
static int exec_image(const char *name, unsigned long len, void *image, unsigned long size) {
    register unsigned long a0 asm("a0") = SYS_exec;
    register unsigned long a1 asm("a1") = (unsigned long)name;
    register unsigned long a2 asm("a2") = len;
    register unsigned long a3 asm("a3") = (unsigned long)image;
    register unsigned long a4 asm("a4") = size;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4) : "memory");
    return (int)a0;
}
int main(void) {
    int code = 0; int pid = fork(); assert(pid >= 0);
    if (!pid) { char image[64] = {0}; exec_image("bad", 3, image, sizeof(image)); panic("invalid exec returned"); }
    assert(waitpid(pid, &code) == 0); assert(code == -E_INVAL_ELF);
    cprintf("b_execfail pass.\n"); return 0;
}
