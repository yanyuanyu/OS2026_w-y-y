#include <proc.h>
#include <sched.h>
#include <pmm.h>
#include <sync.h>
#include <string.h>
#include <sbi.h>
#include <stdio.h>
#include <assert.h>

extern int c_check_saved_registers(void);
static volatile int completed[2];
static uintptr_t root_pa[2];
#define TEST_LA ((uintptr_t)0x40000000)
static volatile int idle_pending, idle_seen;
static void c_idle_hook(void)
{
    if (idle_pending) {
        assert(current == idleproc);
        idle_pending = 0;
        idle_seen = 1;
        wakeup_proc(initproc);
        current->need_resched = 1;
    }
}

static int c_worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < 64; ++i) {
        assert(read_csr(satp) == (((uintptr_t)SATP_MODE_SV39 << 60) |
                                (root_pa[id] >> RISCV_PGSHIFT)));
        assert(*(volatile uintptr_t *)TEST_LA == (uintptr_t)(0x1230 + id));
        assert(read_csr(sstatus) & SSTATUS_SIE);
        assert(c_check_saved_registers());
        completed[id]++;
    }
    current->state = PROC_SLEEPING;
    schedule();
    panic("sleeping test worker was scheduled");
}

static void c_test_main(void)
{
    cprintf("[C TEST] start real RV64/Sv39 validation\n");
    bool outer, inner;
    assert(read_csr(sstatus) & SSTATUS_SIE);
    local_intr_save(outer);
    local_intr_save(inner);
    assert(outer && !inner && !(read_csr(sstatus) & SSTATUS_SIE));
    local_intr_restore(inner);
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    local_intr_restore(outer);
    assert(read_csr(sstatus) & SSTATUS_SIE);
    cprintf("[C TEST] nested interrupt restore: PASS\n");

    int runs = current->runs;
    schedule();  // only init is in proc_list: choose itself, no context switch
    assert(current == initproc && current->runs == runs + 1);
    assert(read_csr(sstatus) & SSTATUS_SIE);
    cprintf("[C TEST] self selection and runs: PASS\n");

    // Both workers get distinct root physical pages with identical kernel mappings.
    for (int i = 0; i < 2; ++i) {
        struct Page *page = alloc_page();
        assert(page != NULL);
        memcpy(page2kva(page), boot_pgdir_va, PGSIZE);
        root_pa[i] = page2pa(page);
        struct Page *data = alloc_page();
        assert(data != NULL);
        *(uintptr_t *)page2kva(data) = 0x1230 + i;
        assert(page_insert(page2kva(page), data, TEST_LA, PTE_R | PTE_W) == 0);
        int pid = kernel_thread(c_worker, (void *)(intptr_t)i, 0);
        assert(pid > 0);
        find_proc(pid)->pgdir = root_pa[i];
    }
    local_intr_save(outer);
    assert(c_check_saved_registers());  // real switch away while SIE is disabled
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    local_intr_restore(outer);
    for (int i = 0; i < 70; ++i) {
        assert(c_check_saved_registers());
        assert(read_csr(satp) == (((uintptr_t)SATP_MODE_SV39 << 60) |
                                (boot_pgdir_pa >> RISCV_PGSHIFT)));
    }
    assert(completed[0] == 64 && completed[1] == 64);
    cprintf("[C TEST] workers completed: 64 / 64\n");
    cprintf("[C TEST] ra/sp/s0-s11 across repeated switches: PASS\n");
    cprintf("[C TEST] distinct Sv39 roots and TLB refresh: PASS\n");
    cprintf("[C TEST] sleeping threads skipped: PASS\n");

    local_intr_save(outer);
    schedule();
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    local_intr_restore(outer);
    cprintf("[C TEST] SIE-off caller remains disabled: PASS\n");
    idle_pending = 1;
    current->state = PROC_SLEEPING;
    schedule();
    assert(idle_seen && current == initproc);
    cprintf("[C TEST] idle fallback and cpu_idle redispatch: PASS\n");
    cprintf("[C TEST] ALL PASS\n");
    sbi_shutdown();
    while (1) {}
}
