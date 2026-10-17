/* A repeatable real store pair for guest/host GDB, not a simulated result. */
#include <pmm.h>

volatile uintptr_t c_probe_word __attribute__((aligned(PGSIZE)));

void __noinline c_memory_probe(void) {
    /* Make the first data access cold; the second has no intervening flush. */
    asm volatile("sfence.vma" ::: "memory");
    c_probe_word = 0x1122334455667788ULL;
    c_probe_word = 0x8877665544332211ULL;
}
