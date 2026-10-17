#include <pmm.h>
#include <stdio.h>
#include "default_pmm.c"

static struct Page descriptors[32];
struct Page *pages = descriptors;
const size_t nbase = DRAM_BASE / PGSIZE;
size_t npage = DRAM_BASE / PGSIZE + 32;
uint64_t va_pa_offset;
const struct pmm_manager *pmm_manager = &default_pmm_manager;
struct Page *alloc_pages(size_t n) { return pmm_manager->alloc_pages(n); }
void free_pages(struct Page *p, size_t n) { pmm_manager->free_pages(p, n); }
size_t nr_free_pages(void) { return pmm_manager->nr_free_pages(); }

static void reset(void) {
    default_init();
    for (size_t i = 0; i < 32; ++i) {
        pages[i].flags = pages[i].property = pages[i].ref = 0;
        SetPageReserved(pages + i);
    }
}
static void check_links(size_t expected) {
    size_t count = 0, total = 0;
    struct Page *previous = NULL;
    for (list_entry_t *le = list_next(&free_list); le != &free_list; le = list_next(le)) {
        assert(++count <= 32);
        assert(le->next->prev == le && le->prev->next == le);
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p) && (previous == NULL || previous < p));
        total += p->property;
        previous = p;
    }
    assert(total == expected && total == nr_free_pages());
}
int main(void) {
    reset();
    default_init_memmap(pages, 2);
    default_init_memmap(pages + 8, 2);
    check_links(4);
    assert(alloc_pages(2) == pages);
    assert(alloc_pages(2) == pages + 8);
    assert(alloc_pages(1) == NULL);
    cprintf("FIRSTFIT multi-range tail insertion PASS\n");

    reset();
    default_init_memmap(pages, 8);
    struct Page *a = alloc_pages(2), *b = alloc_pages(2), *c = alloc_pages(2), *d = alloc_pages(2);
    assert(a == pages && b == pages + 2 && c == pages + 4 && d == pages + 6);
    free_pages(a, 2);
    free_pages(c, 2);
    check_links(4);
    free_pages(b, 2);
    check_links(6);
    free_pages(d, 2);
    check_links(8);
    assert(alloc_pages(8) == pages && nr_free_pages() == 0);
    cprintf("FIRSTFIT free-tail and two-sided coalescing PASS\n");
    return 0;
}
