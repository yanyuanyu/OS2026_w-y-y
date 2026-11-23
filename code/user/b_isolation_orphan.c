#include <stdio.h>
#include <ulib.h>
static volatile int value = 7;
int main(void) {
    int pid, code = -1;
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) { value = 42; exit(23); }
    assert(waitpid(pid, &code) == 0 && code == 23);
    assert(value == 7);
    assert(waitpid(pid, &code) != 0);
    pid = fork(); assert(pid >= 0);
    if (pid == 0) {
        int gc = fork(); assert(gc >= 0);
        if (gc == 0) { for (int i = 0; i < 5; i++) yield(); exit(19); }
        exit(31);
    }
    assert(waitpid(pid, &code) == 0 && code == 31);
    cprintf("b_isolation_orphan pass.\n");
    return 0;
}
