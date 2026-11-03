import pathlib, subprocess, tempfile
src = pathlib.Path('kern/trap/trap.c').read_text()
start = src.index('void exception_handler(')
brace = src.index('{', start)
depth = 1
end = brace + 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
func = src[start:end]
harness = r'''
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#define CAUSE_ILLEGAL_INSTRUCTION 2
#define CAUSE_BREAKPOINT 3
struct trapframe { uintptr_t cause, epc; };
static int fallback, messages;
void print_trapframe(struct trapframe *tf) { (void)tf; fallback++; }
int cprintf(const char *fmt, ...) { (void)fmt; messages++; return 0; }
''' + func + r'''
int main(void) {
    uint16_t short_insn[] = {0x9002};
    uint16_t long_insn[] = {0x007b, 0};
    struct trapframe tf;
    for (int cause = 2; cause <= 3; cause++) {
        tf.cause = cause; tf.epc = (uintptr_t)short_insn;
        exception_handler(&tf); assert(tf.epc == (uintptr_t)short_insn + 2);
        tf.epc = (uintptr_t)long_insn;
        exception_handler(&tf); assert(tf.epc == (uintptr_t)long_insn + 4);
    }
    assert(messages >= 4);
    tf.cause = 20; tf.epc = 42; exception_handler(&tf);
    assert(fallback == 1 && tf.epc == 42);
    puts("exception handler tests: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    p = pathlib.Path(d); (p/'test.c').write_text(harness)
    subprocess.run(['gcc','-std=c99','-Wall','-Werror',str(p/'test.c'),'-o',str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
