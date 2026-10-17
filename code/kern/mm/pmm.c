#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
#ifdef USE_BUDDY_PMM
    pmm_manager = &buddy_pmm_manager;
#elif defined(USE_FIRST_FIT_PMM)
    pmm_manager = &default_pmm_manager;
#else
    pmm_manager = &best_fit_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 - 王优2413681 严苑毓2413682 杨思远2413636
/*
 * page_init - discover physical memory, reserve kernel metadata, and give the
 * remaining complete pages to the selected physical-memory manager.
 */
static void page_init(void) {
    extern char end[];
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t mem_end;
    size_t page_count;
    uintptr_t pages_va, pages_end_va, free_pa;

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    if (mem_base == 0 || mem_size == 0 || mem_size > ~(uint64_t)0 - mem_base) {
        panic("page_init: invalid physical memory range");
    }
    mem_end = mem_base + mem_size;
    if ((mem_base & (PGSIZE - 1)) != 0 || mem_base / PGSIZE != nbase) {
        panic("page_init: unsupported or unaligned physical memory range");
    }
    /* entry.S maps only the DRAM-start 1 GiB window with a root leaf PTE. */
    if (mem_end > (uint64_t)DRAM_BASE + (1ULL << 30)) {
        panic("page_init: physical memory outside boot mapping");
    }
    /* An incomplete tail page must not be advertised to the allocator. */
    mem_end = ROUNDDOWN(mem_end, PGSIZE);

    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no physical pages available");
    }
    page_count = npage - nbase;

    pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    if (page_count > (~(uintptr_t)0 - pages_va) / sizeof(struct Page)) {
        panic("page_init: page descriptor array size overflow");
    }
    pages_end_va = pages_va + page_count * sizeof(struct Page);
    if (pages_end_va < pages_va) {
        panic("page_init: page descriptor array overflow");
    }
    if (pages_end_va > ~(uintptr_t)0 - (PGSIZE - 1)) {
        panic("page_init: page descriptor alignment overflow");
    }
    free_pa = PADDR(ROUNDUP(pages_end_va, PGSIZE));
    if (PADDR(pages_va) < mem_base || free_pa < mem_base || free_pa >= mem_end) {
        panic("page_init: kernel and page descriptors consume physical memory");
    }
    pages = (struct Page *)pages_va;

    /* Reserve all pages first. init_memmap clears the state of free pages. */
    for (size_t i = 0; i < page_count; i++) {
        pages[i].flags = 0;
        pages[i].property = 0;
        set_page_ref(&pages[i], 0);
        SetPageReserved(&pages[i]);
    }

    cprintf("Physical memory: [0x%016lx, 0x%016lx)\n",
            (unsigned long)mem_base, (unsigned long)mem_end);
    cprintf("Page descriptors: %lu pages at 0x%016lx (virtual)\n",
            (unsigned long)page_count, (unsigned long)pages_va);
    cprintf("Free physical memory begins at 0x%016lx\n",
            (unsigned long)free_pa);

    pmm_manager->init_memmap(pa2page(free_pa), npage - PPN(free_pa));
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
