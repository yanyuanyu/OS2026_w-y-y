#include <stdio.h>
#include <ulib.h>
int main(void) {
    int pid = fork(), code = -1;
    assert(pid >= 0);
    if (pid == 0) exit(37);
    assert(waitpid(pid, &code) == 0 && code == 37);
    assert(waitpid(pid, &code) != 0);
    cprintf("b_lifecycle pass.\n");
    return 0;
}
