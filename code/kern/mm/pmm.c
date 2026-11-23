#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <dtb.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>
#include <proc.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir_va = NULL;
// physical address of boot-time page directory
uintptr_t boot_pgdir_pa;

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

// A: 从已有 Lab4 迁移 DTB 内存初始化。
static void page_init(void) {
    // end 是链接脚本给出的内核镜像末尾虚拟地址，不是物理内存的终点。
    extern char end[];
    // 取 DTB 已解析出的 DRAM 起始物理地址和长度（字节）。
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t mem_end;
    size_t page_count;
    uintptr_t pages_va, pages_end_va, free_pa;

    // 设置直映固定偏移，之后 PADDR 用它把高半区内核地址换回物理地址。
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    // 拒绝缺失的内存范围；~(uint64_t)0 是最大无符号值，用减法预先检查 base + size 是否溢出。
    if (mem_base == 0 || mem_size == 0 || mem_size > ~(uint64_t)0 - mem_base) {
        panic("page_init: invalid physical memory range");
    }
    // 采用半开区间 [mem_base, mem_end)，mem_end 本身不属于 DRAM。
    mem_end = mem_base + mem_size;
    // 页大小是 2 的幂，低位非零说明起点未对齐；起始物理页号还必须等于框架固定的 nbase。
    if ((mem_base & (PGSIZE - 1)) != 0 || mem_base / PGSIZE != nbase) {
        panic("page_init: unsupported or unaligned physical memory range");
    }
    // 启动页表的临时大页覆盖 DRAM 起始处的 1 GiB 窗口。
    if (mem_end > (uint64_t)DRAM_BASE + (1ULL << 30)) {
        panic("page_init: physical memory outside boot mapping");
    }
    // 向下取整到页边界，不把末尾不足一页的区域交给分配器。
    mem_end = ROUNDDOWN(mem_end, PGSIZE);

    // npage 是末端物理页号上界（不含），不是 DRAM 总页数；总页数等于 npage - nbase。
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no physical pages available");
    }
    page_count = npage - nbase;

    // 把 Page 元数据数组放在内核镜像之后，起点按页对齐。每个物理页框对应一个 Page，包括保留页。
    pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    // 先验证从 pages_va 起能容纳整个数组，避免计算元素总字节数或终点时回绕。
    if (page_count > (~(uintptr_t)0 - pages_va) / sizeof(struct Page)) {
        panic("page_init: page descriptor array size overflow");
    }
    // 数组末尾的虚拟地址（不含）；它可能落在某个物理页中间。
    pages_end_va = pages_va + page_count * sizeof(struct Page);
    // 再检查数组终点是否因无符号回绕而落到起点之前。
    if (pages_end_va < pages_va) {
        panic("page_init: page descriptor array overflow");
    }
    // ROUNDUP 之前检查加 PGSIZE - 1 是否会溢出。
    if (pages_end_va > ~(uintptr_t)0 - (PGSIZE - 1)) {
        panic("page_init: page descriptor alignment overflow");
    }
    // 越过数组占用的最后一页，再把对齐后的虚拟地址转换为第一张可分配页的物理地址。
    free_pa = PADDR(ROUNDUP(pages_end_va, PGSIZE));
    // 数组起点和可分配区起点都应位于 DRAM；free_pa 必须严格小于 mem_end，至少留下一个整页。
    if (PADDR(pages_va) < mem_base || free_pa < mem_base || free_pa >= mem_end) {
        panic("page_init: kernel and page descriptors consume physical memory");
    }
    // 设置全局描述符数组基址；物理页号减 nbase 就是其 Page 下标。
    pages = (struct Page *)pages_va;

    // 先把每个 Page 标为保留；init_memmap 再把实际空闲的页交给分配器。
    for (size_t i = 0; i < page_count; i++) {
        // 先清状态、空闲块长度及引用计数，再设置保留位。此时还没有任何页加入空闲链表。
        pages[i].flags = 0;
        pages[i].property = 0;
        set_page_ref(&pages[i], 0);
        SetPageReserved(&pages[i]);
    }

    // 输出可用来核对布局的三项信息：DRAM 半开区间、描述符数量与虚拟基址、首个空闲页物理地址。
    cprintf("Physical memory: [0x%016lx, 0x%016lx)\n",
            (unsigned long)mem_base, (unsigned long)mem_end);
    cprintf("Page descriptors: %lu pages at 0x%016lx (virtual)\n",
            (unsigned long)page_count, (unsigned long)pages_va);
    cprintf("Free physical memory begins at 0x%016lx\n",
            (unsigned long)free_pa);

    // 把 [free_pa, mem_end) 交给当前 PMM：首个 Page 由 pa2page 得到，页数为上界减起始页号。之前的页保持保留。
    pmm_manager->init_memmap(pa2page(free_pa), npage - PPN(free_pa));
}

static void enable_paging(void)
{
    write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
}

// boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void)
{
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size).
    // So a framework of physical memory manager (struct pmm_manager)is defined
    // in pmm.h
    // First we should init a physical memory manager(pmm) based on the
    // framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    check_alloc_page();

    // create boot_pgdir, an initial page directory(Page Directory Table, PDT)
    extern char boot_page_table_sv39[];
    /* Replace the temporary 1 GiB leaf with a three-level 4 KiB mapping. */
    boot_pgdir_va = boot_alloc_page();
    memset(boot_pgdir_va, 0, PGSIZE);
    boot_pgdir_pa = PADDR(boot_pgdir_va);
    boot_map_segment(boot_pgdir_va, DRAM_BASE + va_pa_offset,
                     npage * PGSIZE - DRAM_BASE, DRAM_BASE,
                     PTE_R | PTE_W | PTE_X);
    enable_paging();
    flush_tlb();

    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.
    check_boot_pgdir();

    kmalloc_init();
}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    // PDX1 取 VA[38:30]，pdep1 是根页表中对应表项的内核虚拟地址。
    pde_t *pdep1 = &pgdir[PDX1(la)];
    // 记录本次新建的表，用于第二次分配失败时回滚。
    struct Page *new_middle = NULL;
    // V=0 表示此路径缺少中间页表；根表本身由 pgdir 提供，不在这里分配。
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        // 短路求值：create=false 时不会调用 alloc_page；允许创建但内存不足也返回 NULL。
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        // 新页由父级目录项引用。Page 描述符转物理地址后，再经 KADDR 取得可写的内核直映地址。
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        // 发布到父表之前先清零整页，保证新表中的所有槽位初始都无效。
        memset(KADDR(pa), 0, PGSIZE);
        // 父项保存下一级表的物理页号和 V 标志；R/W/X 保持为 0，表示该项不是叶子映射。
        *pdep1 = pte_create(page2ppn(page), PTE_V);
        // 记录资源归属：只记录本次分配的中间级页表，不记录原有页表。
        new_middle = page;
    }
    // PDE_ADDR 从根表项还原下一级表的物理基址，KADDR 转为内核虚拟地址；PDX0 取 VA[29:21]。
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    // V=0 表示缺少最低级页表；这里建的是存放叶子 PTE 的表，不是映射目标数据页。
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        // 短路求值：create=false 时不会调用 alloc_page；允许创建但内存不足也返回 NULL。
        if (!create || (page = alloc_page()) == NULL)
        {
            // 仅当第二次分配失败、且本次确实新建了中间表才回滚。原有表可能服务于其他映射，必须保留。
            if (new_middle != NULL)
            {
                // 先从根表断开新建的中间表，再清引用并归还物理页，避免留下悬空目录项。
                *pdep1 = 0;
                set_page_ref(new_middle, 0);
                free_page(new_middle);
            }
            return NULL;
        }
        // 新页由父级目录项引用。Page 描述符转物理地址后，再经 KADDR 取得可写的内核直映地址。
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        // 发布到父表之前先清零整页，保证新表中的所有槽位初始都无效。
        memset(KADDR(pa), 0, PGSIZE);
        // 把已清零的最低级页表挂到中间表；其中的目标叶子表项仍未填入映射。
        *pdep0 = pte_create(page2ppn(page), PTE_V);
    }
    // 用最低 9 位 VPN 定位叶子表项；这里返回指针，不解释其权限。
    // PTX 取 VA[20:12]；低 12 位页内偏移不参与选表项。返回的是 PTE 槽位的指针，不是数据页地址。
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

// page_remove_pte - free an Page sturct which is related linear address la
//                - and clean(invalidate) pte which is related linear address la
// note: PT is changed, so the TLB need to be invalidate
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V)
    {
        struct Page *page = pte2page(*ptep);
        page_ref_dec(page);
        if (page_ref(page) == 0)
        {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    do
    {
        pte_t *ptep = get_pte(pgdir, start, 0);
        if (ptep == NULL)
        {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*ptep != 0)
        {
            page_remove_pte(pgdir, start, ptep);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
}

/* A: 只释放确实全空的页表，逐级检查整个表，避免跨 VMA 提前释放或泄漏。 */
void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));
    for (uintptr_t la = ROUNDDOWN(start, PTSIZE); la < end; la += PTSIZE) {
        pde_t *root = &pgdir[PDX1(la)];
        if (!(*root & PTE_V)) continue;
        assert(!(*root & (PTE_R | PTE_W | PTE_X)));
        pde_t *middle = KADDR(PDE_ADDR(*root));
        pde_t *entry = &middle[PDX0(la)];
        if (*entry & PTE_V) {
            assert(!(*entry & (PTE_R | PTE_W | PTE_X)));
            pte_t *table = KADDR(PDE_ADDR(*entry));
            bool empty = 1;
            for (int i = 0; i < NPTEENTRY; i++)
                if (table[i] != 0) { empty = 0; break; }
            if (empty) {
                struct Page *p = pde2page(*entry);
                *entry = 0; set_page_ref(p, 0); free_page(p);
            }
        }
        bool empty = 1;
        for (int i = 0; i < NPDEENTRY; i++)
            if (middle[i] != 0) { empty = 0; break; }
        if (empty) {
            struct Page *p = pde2page(*root);
            *root = 0; set_page_ref(p, 0); free_page(p);
        }
    }
}

/* A: 分配阶段不动父 PTE，保证跨 VMA 的 fork 失败也不改变父权限。
 * 空洞不分配数据页；to 必须是独立、新建且目标区间为空的页表。 */
//lab5 2413636：copy_range_prepare
int copy_range_prepare(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end)
{
    assert(to != from && start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));
    while (start < end) {
        pte_t *src = get_pte(from, start, 0);
        if (src == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*src & PTE_V) {
            pte_t *dst = get_pte(to, start, 1);
            if (dst == NULL) return -E_NO_MEM;
            assert(*dst == 0);
        }
        start += PGSIZE;
    }
    return 0;
}

/* share=true: COW 共享；share=false: 立即深拷贝（保留普通复制测试接口）。
 * COW 提交阶段无内存分配；调用者在 mm 锁/关中断下使用。
 * 单独调用失败时，调用者负责 unmap_range/exit_range 清理目标半成品。 */
//lab5 2413636：copy_range
int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share)
{
    int ret = copy_range_prepare(to, from, start, end);
    if (ret != 0) return ret;
    while (start < end) {
        pte_t *src = get_pte(from, start, 0);
        if (src == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*src & PTE_V) {
            struct Page *page = pte2page(*src);
            uint32_t perm = *src & 0x3ff;
            if (share) {
                if (perm & PTE_W) perm = (perm & ~PTE_W) | PTE_COW;
                ret = page_insert(to, page, start, perm);
                assert(ret == 0); /* prepare 已保证页表齐全 */
                if (*src & PTE_W) {
                    *src = (*src & ~PTE_W) | PTE_COW;
                    tlb_invalidate(from, start);
                }
            } else {
                struct Page *copy = alloc_page();
                if (copy == NULL) return -E_NO_MEM;
                memcpy(page2kva(copy), page2kva(page), PGSIZE);
                if (perm & PTE_COW) perm = (perm | PTE_W) & ~PTE_COW;
                ret = page_insert(to, copy, start, perm);
                if (ret != 0) { free_page(copy); return ret; }
            }
        }
        start += PGSIZE;
    }
    return 0;
}


// A: 迁移已有 Lab4 页表插入/移除。
void page_remove(pde_t *pgdir, uintptr_t la)
{
    // create=0，只查找已有页表路径。若缺少某级下级页表则返回 NULL，不为删除而新建页表。
    pte_t *ptep = get_pte(pgdir, la, 0);
    // 指针存在只表示找到了槽位；是否有有效映射由辅助函数检查 PTE_V。
    if (ptep != NULL)
    {
        page_remove_pte(pgdir, la, ptep);
    }
}

int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    // create=1：定位目标 PTE；如果路径上的页表不存在，允许分配页表页，但不会分配目标数据页。
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL)
    {
        // 找不到可用的页表槽位，报告内存不足。此时还没有增加目标页的引用计数。
        return -E_NO_MEM;
    }
    // 先增加新页引用；旧映射指向同一物理页时抵消此次增加，不释放该页。
    // 先为即将写入的新映射增加一次引用；若原表项无效，不处理旧页，直接在后面写入新映射。
    page_ref_inc(page);
    // 槽位存在不等于已有映射；只有 V 置位才需要检查原来指向哪张物理页。
    if (*ptep & PTE_V)
    {
        // 同一虚拟页原本就指向 page：这是重设表项权限，而不是增加另一条映射。
        if (pte2page(*ptep) == page)
        {
            // 抵消刚才加的一次引用，最终计数不变；不调用删除辅助函数，不释放原页。
            page_ref_dec(page);
        }
        else
        {
            // 异页替换：先撤销旧映射并使旧翻译失效；若旧页引用归零，由辅助函数释放旧页。
            page_remove_pte(pgdir, la, ptep);
        }
    }
    // 写入目标物理页号和新权限；随后使该虚拟地址的旧翻译失效。
    // page2ppn 取得目标物理页号，pte_create 组合页号、perm 和有效位 V；此处整个 PTE 被覆盖。
    *ptep = pte_create(page2ppn(page), perm);
    // 写入新 PTE 后按 la 使本地 TLB 翻译失效，避免同页改权或异页替换后继续使用旧翻译。
    tlb_invalidate(pgdir, la);
    // 页表项更新完成，返回 0 表示成功。
    return 0;
}


// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    asm volatile("sfence.vma %0" : : "r"(la));
}

// pgdir_alloc_page - call alloc_page & page_insert functions to
//                  - allocate a page size memory & setup an addr map
//                  - pa<->la with linear address la and the PDT pgdir
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm)
{
    struct Page *page = alloc_page();
    if (page != NULL)
    {
        if (page_insert(pgdir, page, la, perm) != 0)
        {
            free_page(page);
            return NULL;
        }
        // swap_map_swappable(check_mm_struct, la, page, 0);
        page->pra_vaddr = la;
        assert(page_ref(page) == 1);
        // cprintf("get No. %d  page: pra_vaddr %x, pra_link.prev %x,
        // pra_link_next %x in pgdir_alloc_page\n", (page-pages),
        // page->pra_vaddr,page->pra_page_link.prev,
        // page->pra_page_link.next);
    }

    return page;
}

static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert((boot_pgdir_va[0] & (PTE_U | PTE_A | PTE_D)) == 0);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - use string 'u,r,w,-' to present the permission
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - In [left, right] range of PDT or PT, find a continuous
// linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
//  return value: 0 - not a invalid item range, perm - a valid item range with
//  perm permission
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}

// Diagnostic root entries for SYS_pgdir (integration support).
void print_pgdir(void)
{
    extern struct proc_struct *current;
    pde_t *pgdir = current != NULL && current->mm != NULL ? current->mm->pgdir : boot_pgdir_va;
    cprintf("page directory:\n");
    for (size_t i = 0; i < NPTEENTRY; i++)
        if (pgdir[i] & PTE_V) cprintf("  [%lu] = 0x%lx\n", i, pgdir[i]);
}

