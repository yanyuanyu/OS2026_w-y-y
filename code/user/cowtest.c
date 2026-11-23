/* 多代 fork、COW 写入隔离、wait 退出码与单引用恢复的用户态集成测试。 */
#include <ulib.h>
#include <stdio.h>

static volatile unsigned char data[8192] __attribute__((aligned(4096)));

int main(void)
{
    data[0] = 17;
    data[4096] = 23;
    int pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        assert(data[0] == 17 && data[4096] == 23);
        data[0] = 42;
        int grandchild = fork();
        assert(grandchild >= 0);
        if (grandchild == 0) {
            assert(data[0] == 42 && data[4096] == 23);
            data[0] = 99;
            data[4096] = 88;
            exit(7);
        }
        int status = -1;
        assert(waitpid(grandchild, &status) == 0 && status == 7);
        assert(data[0] == 42 && data[4096] == 23);
        exit(9);
    }
    int status = -1;
    assert(waitpid(pid, &status) == 0 && status == 9);
    assert(data[0] == 17 && data[4096] == 23);
    data[0] = 55; /* 子孙退出后，父进程单引用页的再次写入 */
    assert(data[0] == 55);
    cprintf("cowtest pass.\n");
    return 0;
}
