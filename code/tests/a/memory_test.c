#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <pmm.h>
#include <vmm.h>
#include <dtb.h>
#include <console.h>
#include <error.h>
#include <riscv.h>
#include <sbi.h>

void test_lab4_pmm_basic(void);

/* This fixture stops on unexpected traps instead of implementing Lab3/B/C. */
static void unexpected_trap(void) __attribute__((aligned(4), noreturn));
static void unexpected_trap(void)
{
    panic("A test trap: cause=%lx epc=%lx tval=%lx", read_csr(scause),
          read_csr(sepc), read_csr(stval));
}

static void test_out_of_memory(void)
{
    /* Retain the real allocator, but temporarily hold every available page. */
    size_t before = nr_free_pages();
    pde_t *root = page2kva(alloc_page());
    struct Page *data = alloc_page();
    assert(root != NULL && data != NULL);
    memset(root, 0, PGSIZE);
    list_entry_t held;
    list_init(&held);
    struct Page *p;
    while ((p = alloc_page()) != NULL) list_add(&held, &p->page_link);
    assert(page_insert(root, data, 0, PTE_R) == -E_NO_MEM);
    assert(root[0] == 0 && page_ref(data) == 0);
    /* One page is enough for VPN[2]'s table but not VPN[1]'s table. */
    list_entry_t *le = list_next(&held);
    list_del(le);
    free_page(le2page(le, page_link));
    assert(page_insert(root, data, 0, PTE_R) == -E_NO_MEM);
    assert(root[0] == 0 && nr_free_pages() == 1);
    /* A pre-existing intermediate table belongs to its caller, not rollback. */
    assert(get_pte(root, 0, 1) == NULL && root[0] == 0);
    struct Page *middle = alloc_page();
    memset(page2kva(middle), 0, PGSIZE);
    set_page_ref(middle, 1);
    root[0] = pte_create(page2ppn(middle), PTE_V);
    pte_t saved = root[0];
    assert(page_insert(root, data, 0, PTE_R) == -E_NO_MEM);
    assert(root[0] == saved && page_ref(middle) == 1);
    root[0] = 0;
    set_page_ref(middle, 0);
    free_page(middle);
    while (!list_empty(&held)) {
        le = list_next(&held);
        list_del(le);
        free_page(le2page(le, page_link));
    }
    free_page(data);
    free_page(kva2page(root));
    assert(nr_free_pages() == before);
    cprintf("[Test A OOM rollback] PASS\n");
}

static void test_tlb(void)
{
    size_t before = nr_free_pages();
    uintptr_t va = 0x40000000;
    struct Page *a = alloc_page(), *b = alloc_page();
    assert(a && b);
    *(volatile int *)page2kva(a) = 11;
    *(volatile int *)page2kva(b) = 22;
    assert(page_insert(boot_pgdir_va, a, va, PTE_R | PTE_W) == 0);
    assert(*(volatile int *)va == 11); /* Warm the real CPU TLB. */
    assert(page_insert(boot_pgdir_va, b, va, PTE_R | PTE_W) == 0);
    assert(*(volatile int *)va == 22);
    page_remove(boot_pgdir_va, va);
    assert(get_page(boot_pgdir_va, va, NULL) == NULL);
    pde_t *middle = KADDR(PDE_ADDR(boot_pgdir_va[PDX1(va)]));
    free_page(pde2page(middle[PDX0(va)]));
    free_page(pde2page(boot_pgdir_va[PDX1(va)]));
    boot_pgdir_va[PDX1(va)] = 0;
    flush_tlb();
    assert(nr_free_pages() == before);
    cprintf("[Test A TLB replacement] PASS\n");
}

int kern_init(void)
{
    extern char edata[], end[];
    extern pde_t boot_page_table_sv39[];
    /* Drop the test-only identity mapping after reaching the high address. */
    boot_page_table_sv39[2] = 0;
    flush_tlb();
    memset(edata, 0, end - edata);
    cons_init();
    write_csr(stvec, (uintptr_t)unexpected_trap);
    dtb_init();
    pmm_init();
    vmm_init();
    test_lab4_pmm_basic();
    size_t before = nr_free_pages();
    page_remove(boot_pgdir_va, 1ULL << 38);
    assert(nr_free_pages() == before);
    test_out_of_memory();
    test_tlb();
    cprintf("PART A PASS: memory subsystem only; B/C not executed\n");
    sbi_shutdown();
    while (1) {}
}
