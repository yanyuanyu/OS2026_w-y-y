/* White-box module harness: production proc.c is included to inspect static
 * helpers. B/C startup, scheduling and trap delivery are deliberately absent. */
#include <sbi.h>
#include <dtb.h>
#include <console.h>
#include <stdarg.h>
#define schedule a_test_schedule
#include "../../kern/process/proc.c"

static long page_budget = -1, heap_budget = -1;
static struct pmm_manager failing_manager;
static const struct pmm_manager *real_manager;
void *__real_kmalloc(size_t);
void *__wrap_kmalloc(size_t n) {
    if (heap_budget == 0) return NULL;
    if (heap_budget > 0) heap_budget--;
    return __real_kmalloc(n);
}
static struct Page *budget_alloc(size_t n) {
    if (page_budget == 0) return NULL;
    if (page_budget > 0) page_budget--;
    return real_manager->alloc_pages(n);
}
void schedule(void) { panic("unexpected scheduler entry in A module test"); }
void forkrets(struct trapframe *tf) { panic("B/C trap return is outside this test"); }
void __panic(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    cprintf("[A] FAIL %s:%d ", file, line);
    va_start(ap, fmt); vcprintf(fmt, ap); va_end(ap);
    cprintf("\n"); sbi_shutdown(); while (1) {}
}
void __warn(const char *file, int line, const char *fmt, ...) {
    panic("unexpected warning at %s:%d", file, line);
}

static struct mm_struct *new_mm(void) {
    struct mm_struct *mm = mm_create();
    assert(mm != NULL && setup_pgdir(mm) == 0);
    return mm;
}
static void delete_mm(struct mm_struct *mm) {
    assert(mm_count(mm) == 0);
    exit_mmap(mm); put_pgdir(mm); mm_destroy(mm);
}
static pte_t *slot(struct mm_struct *mm, uintptr_t va) {
    pte_t *p = get_pte(mm->pgdir, va, 0);
    assert(p != NULL && (*p & PTE_V)); return p;
}
static struct Page *map_page(struct mm_struct *mm, uintptr_t va, int flags) {
    assert(mm_map(mm, va, PGSIZE, flags, NULL) == 0);
    struct Page *p = pgdir_alloc_page(mm->pgdir, va,
        PTE_U | PTE_R | ((flags & VM_WRITE) ? PTE_W : 0));
    assert(p != NULL); memset(page2kva(p), 0x5a, PGSIZE); return p;
}
static void test_cow(void) {
    size_t before = nr_free_pages();
    struct mm_struct *a = new_mm(), *b = new_mm(), *c = new_mm();
    uintptr_t va = UTEXT;
    struct Page *p = map_page(a, va, VM_READ | VM_WRITE);
    struct Page *ro = map_page(a, va + 2 * PGSIZE, VM_READ);
    assert(dup_mmap(b, a) == 0);
    assert(get_page(b->pgdir, va, NULL) == p && page_ref(p) == 2);
    assert((*slot(a, va) & (PTE_COW | PTE_W)) == PTE_COW);
    assert((*slot(b, va) & (PTE_COW | PTE_W)) == PTE_COW);
    assert(get_page(b->pgdir, va + 2 * PGSIZE, NULL) == ro && page_ref(ro) == 2);
    assert(!(*slot(b, va + 2 * PGSIZE) & (PTE_W | PTE_COW)));
    assert(dup_mmap(c, b) == 0 && page_ref(p) == 3);
    assert(cow_pgfault(b, va) == 0);
    struct Page *q = get_page(b->pgdir, va, NULL);
    assert(q != p && page_ref(p) == 2 && page_ref(q) == 1);
    assert(memcmp(page2kva(p), page2kva(q), PGSIZE) == 0);
    *(char *)page2kva(q) = 42;
    assert(*(char *)page2kva(p) == 0x5a);
    assert((*slot(b, va) & (PTE_W | PTE_COW)) == PTE_W);
    assert(cow_pgfault(b, va) == -E_INVAL);
    assert(cow_pgfault(b, va + 2 * PGSIZE) == -E_INVAL);
    pte_t saved_ro = *slot(b, va + 2 * PGSIZE);
    *slot(b, va + 2 * PGSIZE) |= PTE_COW;
    assert(cow_pgfault(b, va + 2 * PGSIZE) == -E_INVAL); /* 即使带标记，VMA 仍只读 */
    assert(*slot(b, va + 2 * PGSIZE) == (saved_ro | PTE_COW));
    *slot(b, va + 2 * PGSIZE) = saved_ro;
    assert(cow_pgfault(b, va + PGSIZE) == -E_INVAL);
    assert(cow_pgfault(NULL, va) == -E_INVAL);
    delete_mm(c);
    assert(page_ref(p) == 1);
    size_t free_now = nr_free_pages();
    assert(cow_pgfault(a, va + 7) == 0);
    assert(get_page(a->pgdir, va, NULL) == p && nr_free_pages() == free_now);
    delete_mm(b); delete_mm(a);
    assert(nr_free_pages() == before);
    cprintf("[A] COW sharing / nested fork / split / singleton / readonly / release PASS\n");
}

static void test_eager(void) {
    size_t before = nr_free_pages();
    struct mm_struct *a = new_mm(), *b = new_mm(), *c = new_mm();
    struct Page *p = map_page(a, UTEXT, VM_READ | VM_WRITE);
    assert(dup_mmap(b, a) == 0);
    assert(mm_map(c, UTEXT, PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    assert(copy_range(c->pgdir, a->pgdir, UTEXT, UTEXT + PGSIZE, 0) == 0);
    struct Page *q = get_page(c->pgdir, UTEXT, NULL);
    assert(q != p && memcmp(page2kva(p), page2kva(q), PGSIZE) == 0);
    assert((*slot(c, UTEXT) & (PTE_W | PTE_COW)) == PTE_W);
    delete_mm(c); delete_mm(b); delete_mm(a);
    assert(nr_free_pages() == before);
    cprintf("[A] eager copy of COW source PASS\n");
}

static void test_oom(void) {
    size_t before = nr_free_pages();
    struct mm_struct *a = new_mm();
    struct Page *p = map_page(a, UTEXT, VM_READ | VM_WRITE);
    map_page(a, UTEXT + PDSIZE, VM_READ | VM_WRITE);
    pte_t old = *slot(a, UTEXT);
    for (int budget = 0; budget < 4; budget++) {
        struct mm_struct *b = new_mm();
        size_t free_now = nr_free_pages();
        page_budget = budget;
        assert(dup_mmap(b, a) == -E_NO_MEM);
        page_budget = -1;
        assert(*slot(a, UTEXT) == old && page_ref(p) == 1);
        assert(b->map_count == 0 && list_empty(&b->mmap_list));
        assert(nr_free_pages() == free_now);
        delete_mm(b);
    }
    struct mm_struct *b = new_mm();
    size_t free_before_heap = nr_free_pages();
    heap_budget = 1; /* second VMA allocation */
    assert(dup_mmap(b, a) == -E_NO_MEM); heap_budget = -1;
    assert(*slot(a, UTEXT) == old && b->map_count == 0);
    assert(nr_free_pages() == free_before_heap);
    assert(dup_mmap(b, a) == 0);
    old = *slot(b, UTEXT);
    size_t free_now = nr_free_pages();
    page_budget = 0;
    assert(cow_pgfault(b, UTEXT) == -E_NO_MEM); page_budget = -1;
    assert(*slot(b, UTEXT) == old && page_ref(p) == 2);
    assert(nr_free_pages() == free_now);
    delete_mm(b); delete_mm(a);
    assert(nr_free_pages() == before);
    cprintf("[A] VMA / page-table / COW allocation failure rollback PASS\n");
}

static void test_live_sibling_table(void) {
    size_t before = nr_free_pages();
    struct mm_struct *a = new_mm();
    map_page(a, UTEXT, VM_READ | VM_WRITE);
    struct Page *keep = map_page(a, UTEXT + PTSIZE, VM_READ | VM_WRITE);
    unmap_range(a->pgdir, UTEXT, UTEXT + PGSIZE);
    exit_range(a->pgdir, UTEXT, UTEXT + PGSIZE);
    assert(get_page(a->pgdir, UTEXT + PTSIZE, NULL) == keep);
    assert(page_ref(keep) == 1);
    delete_mm(a);
    assert(nr_free_pages() == before);
    cprintf("[A] page-table reclamation preserves live sibling PASS\n");
}

static void test_copyout(void) {
    size_t before = nr_free_pages();
    struct mm_struct *a = new_mm(), *b = new_mm();
    struct Page *p = map_page(a, UTEXT, VM_READ | VM_WRITE);
    map_page(a, UTEXT + PGSIZE, VM_READ | VM_WRITE);
    map_page(a, UTEXT + 3 * PGSIZE, VM_READ);
    assert(dup_mmap(b, a) == 0);
    const char str[] = "copyout-boundary";
    /* 故意不切换 satp：copyout 应基于传入 mm 的映射写入，避免直映旁路破坏共享页。 */
    assert(copy_to_user(b, (void *)(UTEXT + PGSIZE - 3), str, sizeof(str)));
    struct Page *q = get_page(b->pgdir, UTEXT, NULL);
    assert(q != p && memcmp((char *)page2kva(q) + PGSIZE - 3, str, 3) == 0);
    q = get_page(b->pgdir, UTEXT + PGSIZE, NULL);
    assert(memcmp(page2kva(q), str + 3, sizeof(str) - 3) == 0);
    assert(((char *)page2kva(p))[PGSIZE - 3] == 0x5a);
    assert(!copy_to_user(b, (void *)(UTEXT + 3 * PGSIZE), str, sizeof(str)));
    assert(!copy_to_user(b, (void *)(UTEXT + 2 * PGSIZE), str, sizeof(str)));
    assert(!copy_to_user(b, (void *)(~(uintptr_t)0 - 2), str, sizeof(str)));
    delete_mm(b); delete_mm(a);
    assert(nr_free_pages() == before);
    cprintf("[A] kernel copyout / cross-page / readonly / holes / overflow PASS\n");
}

extern void a_store_trap(void);
static struct mm_struct *fault_mm;
static int store_faults;
void a_handle_store_fault(uintptr_t cause, uintptr_t addr) {
    assert(cause == CAUSE_STORE_PAGE_FAULT);
    assert(cow_pgfault(fault_mm, addr) == 0);
    store_faults++;
}
static void use_pgdir(pde_t *pgdir) {
    write_csr(satp, (8ULL << 60) | (PADDR(pgdir) >> PGSHIFT));
    flush_tlb();
}
static void test_actual_store(void) {
    size_t before = nr_free_pages();
    struct mm_struct *a = new_mm(), *b = new_mm();
    struct Page *p = map_page(a, UTEXT, VM_READ | VM_WRITE);
    uintptr_t old_vector = read_csr(stvec);
    write_csr(stvec, (uintptr_t)a_store_trap);
    set_csr(sstatus, SSTATUS_SUM);
    use_pgdir(a->pgdir);
    volatile unsigned char *ptr = (void *)UTEXT;
    *ptr = 11; /* 先填充可写 TLB，再 fork 降权；漏刷父 TLB 将被抓出。 */
    assert(dup_mmap(b, a) == 0);
    fault_mm = a;
    *ptr = 22;
    assert(store_faults == 1 && *ptr == 22);
    assert(*(unsigned char *)page2kva(p) == 11);
    fault_mm = b; use_pgdir(b->pgdir);
    assert(*ptr == 11);
    size_t free_now = nr_free_pages();
    *ptr = 33;
    assert(store_faults == 2 && *ptr == 33 && nr_free_pages() == free_now);
    use_pgdir(boot_pgdir_va);
    write_csr(stvec, old_vector);
    clear_csr(sstatus, SSTATUS_SUM);
    delete_mm(b); delete_mm(a);
    assert(nr_free_pages() == before);
    cprintf("[A] real Sv39 store-fault / retry / parent TLB downgrade PASS\n");
}

static void reap_test_child(struct proc_struct *child) {
    if (child->mm && mm_count_dec(child->mm) == 0) delete_mm(child->mm);
    unhash_proc(child); remove_links(child); put_kstack(child); kfree(child);
}
static void test_fork(void) {
    struct proc_struct parent;
    memset(&parent, 0, sizeof(parent));
    parent.pid = 99; parent.state = PROC_RUNNABLE;
    current = &parent;
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++) list_init(&hash_list[i]);
    nr_process = 1;
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf)); tf.gpr.a0 = 123; tf.gpr.sp = USTACKTOP - 64;
    int pid = do_fork(0, 0, &tf);
    assert(pid > 0);
    struct proc_struct *child = find_proc(pid);
    assert(child && child->mm == NULL && child->parent == current);
    assert(child->state == PROC_RUNNABLE && child->tf->gpr.a0 == 0);
    assert(child->tf->gpr.sp == (uintptr_t)child->tf); /* S-mode template */
    assert(tf.gpr.a0 == 123 && child->context.ra == (uintptr_t)forkret);
    reap_test_child(child);
    parent.mm = new_mm(); set_mm_count(parent.mm, 1);
    struct Page *p = map_page(parent.mm, UTEXT, VM_READ | VM_WRITE);
    parent.pgdir = PADDR(parent.mm->pgdir);
    size_t free_now = nr_free_pages();
    heap_budget = 0;
    assert(do_fork(0, tf.gpr.sp, &tf) == -E_NO_MEM); heap_budget = -1;
    page_budget = 0;
    assert(do_fork(0, tf.gpr.sp, &tf) == -E_NO_MEM); page_budget = -1;
    page_budget = 1; /* kernel stack succeeds; pgdir fails */
    assert(do_fork(0, tf.gpr.sp, &tf) == -E_NO_MEM); page_budget = -1;
    for (int budget = 1; budget < 3; budget++) {
        heap_budget = budget; /* mm_struct 或 VMA 分配失败 */
        assert(do_fork(0, tf.gpr.sp, &tf) == -E_NO_MEM); heap_budget = -1;
    }
    page_budget = 3; /* 栈、根表、中间表成功，末级表失败 */
    assert(do_fork(0, tf.gpr.sp, &tf) == -E_NO_MEM); page_budget = -1;
    assert(nr_free_pages() == free_now && parent.cptr == NULL && nr_process == 1);
    pid = do_fork(0, tf.gpr.sp, &tf); child = find_proc(pid);
    assert(child && child->mm != parent.mm && child->tf->gpr.a0 == 0);
    assert(child->tf->gpr.sp == tf.gpr.sp && child->pgdir == PADDR(child->mm->pgdir));
    assert(get_page(child->mm->pgdir, UTEXT, NULL) == p && page_ref(p) == 2);
    reap_test_child(child);
    pid = do_fork(CLONE_VM, tf.gpr.sp, &tf); child = find_proc(pid);
    assert(child && child->mm == parent.mm && mm_count(parent.mm) == 2);
    reap_test_child(child); assert(mm_count(parent.mm) == 1);
    nr_process = MAX_PROCESS;
    assert(do_fork(0, 0, &tf) == -E_NO_FREE_PROC); nr_process = 1;
    set_mm_count(parent.mm, 0); delete_mm(parent.mm);
    current = NULL;
    cprintf("[A] fork PID / child-zero / stack / links / CLONE_VM / failure cleanup PASS\n");
}

void kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    cons_init(); dtb_init(); pmm_init(); vmm_init();
    real_manager = pmm_manager; failing_manager = *pmm_manager;
    failing_manager.alloc_pages = budget_alloc; pmm_manager = &failing_manager;
    test_cow(); test_eager(); test_oom(); test_live_sibling_table();
    test_copyout(); test_actual_store(); test_fork();
    cprintf("[A] ALL PASS\n"); sbi_shutdown(); while (1) {}
}
