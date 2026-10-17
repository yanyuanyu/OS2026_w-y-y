#include <buddy_pmm.h>
#include <stdio.h>

#define CAPACITY 4096
static struct Page descriptors[(size_t)1 << BUDDY_MAX_ORDER];
struct Page *pages = descriptors;
const size_t nbase = DRAM_BASE / PGSIZE;
size_t npage = DRAM_BASE / PGSIZE + CAPACITY;
uint64_t va_pa_offset;
const struct pmm_manager *pmm_manager = &buddy_pmm_manager;
struct Page *alloc_pages(size_t n) { return pmm_manager->alloc_pages(n); }
void free_pages(struct Page *p, size_t n) { pmm_manager->free_pages(p, n); }
size_t nr_free_pages(void) { return pmm_manager->nr_free_pages(); }
/* Host tests exercise the production allocator; memory writes run in QEMU. */
void buddy_check(void) {}

static size_t rounded(size_t n) {
    size_t v = 1;
    while (v < n) v <<= 1;
    return v;
}
static void reset(void) {
    pmm_manager->init();
    for (size_t i = 0; i < CAPACITY; ++i) {
        pages[i].flags = 0;
        pages[i].property = 0;
        pages[i].ref = 0;
        SetPageReserved(pages + i);
    }
}
static void register_range(size_t start, size_t n) {
    pmm_manager->init_memmap(pages + start, n);
    buddy_validate();
}
static void test_sizes(void) {
    reset();
    register_range(0, CAPACITY);
    assert(nr_free_pages() == CAPACITY);
    assert(alloc_pages((size_t)-1) == NULL);
    for (size_t n = 1; n <= 1025; ++n) {
        struct Page *p = alloc_pages(n);
        assert(p != NULL && !PageProperty(p));
        assert(page_ref(p) == 0);
        assert(p->property == rounded(n));
        assert(((size_t)(p - pages) & (rounded(n) - 1)) == 0);
        assert(nr_free_pages() == CAPACITY - rounded(n));
        buddy_validate();
        free_pages(p, n);
        assert(nr_free_pages() == CAPACITY);
        buddy_validate();
    }
    struct Page *a = alloc_pages(CAPACITY);
    assert(a == pages && alloc_pages(1) == NULL);
    free_pages(a, CAPACITY);
    cprintf("HOST sizes, alignment, overflow, restoration PASS\n");
}
static void test_ranges(void) {
    reset();
    register_range(3, 13);
    register_range(32, 32);
    assert(nr_free_pages() == 45);
    struct Page *allocated[45];
    unsigned char seen[CAPACITY] = {0};
    for (int i = 0; i < 45; ++i) {
        allocated[i] = alloc_pages(1);
        assert(allocated[i]);
        size_t index = allocated[i] - pages;
        assert((index >= 3 && index < 16) || (index >= 32 && index < 64));
        assert(!seen[index]);
        seen[index] = 1;
    }
    assert(alloc_pages(1) == NULL);
    for (int i = 44; i >= 0; --i) free_pages(allocated[i], 1);
    buddy_validate();
    assert(nr_free_pages() == 45);
    assert(alloc_pages(64) == NULL);
    for (size_t i = 16; i < 32; ++i) assert(PageReserved(pages + i));
    /* Adjacent separately registered pieces must eventually coalesce. */
    reset();
    register_range(16, 16);
    register_range(0, 16);
    assert(alloc_pages(32) == pages);
    free_pages(pages, 32);
    buddy_validate();
    /* Total free >= request, but all contiguous blocks are too small. */
    reset();
    register_range(0, 8);
    register_range(16, 8);
    assert(nr_free_pages() == 16 && alloc_pages(16) == NULL);
    assert(nr_free_pages() == 16);
    cprintf("HOST unaligned ranges, reserved holes, multi-map, fragmentation PASS\n");
}
static void test_max_order(void) {
    size_t maximum = (size_t)1 << BUDDY_MAX_ORDER;
    npage = nbase + maximum;
    pmm_manager->init();
    for (size_t i = 0; i < maximum; ++i) {
        pages[i].flags = 0;
        pages[i].property = pages[i].ref = 0;
        SetPageReserved(pages + i);
    }
    register_range(0, maximum);
    assert(nr_free_pages() == maximum);
    assert(alloc_pages(maximum + 1) == NULL);
    struct Page *p = alloc_pages(maximum - 1);
    assert(p == pages && p->property == maximum && nr_free_pages() == 0);
    free_pages(p, maximum - 1);
    buddy_validate();
    assert(nr_free_pages() == maximum);
    npage = nbase + CAPACITY;
    cprintf("HOST maximum order and rounding boundary PASS\n");
}
static uint32_t random_state = 0x20c0ffee;
static uint32_t next_random(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}
static void test_random(void) {
    struct Page *live[128] = {0};
    size_t requests[128] = {0};
    unsigned char owner[CAPACITY] = {0};
    size_t used = 0;
    reset();
    register_range(0, CAPACITY);
    for (int step = 0; step < 12000; ++step) {
        size_t slot = (next_random() >> 16) % 128;
        if (live[slot]) {
            size_t index = live[slot] - pages;
            for (size_t j = 0; j < rounded(requests[slot]); ++j) {
                assert(owner[index + j]);
                owner[index + j] = 0;
            }
            free_pages(live[slot], requests[slot]);
            used -= rounded(requests[slot]);
            live[slot] = NULL;
        } else {
            size_t request = ((next_random() >> 16) % 65) + 1;
            struct Page *p = alloc_pages(request);
            if (p) {
                size_t index = p - pages;
                assert(index + rounded(request) <= CAPACITY);
                for (size_t j = 0; j < rounded(request); ++j) {
                    assert(!owner[index + j]);
                    owner[index + j] = 1;
                }
                live[slot] = p;
                requests[slot] = request;
                used += rounded(request);
            }
        }
        assert(nr_free_pages() == CAPACITY - used);
        buddy_validate();
    }
    for (int i = 0; i < 128; ++i)
        if (live[i]) free_pages(live[i], requests[i]);
    buddy_validate();
    assert(nr_free_pages() == CAPACITY && alloc_pages(CAPACITY) == pages);
    cprintf("HOST 12000 seeded operations, bitmap overlap oracle PASS\n");
}
static void invalid_case(char mode) {
    reset();
    register_range(0, 128);
    struct Page *p;
    switch (mode) {
    case 'z': alloc_pages(0); break;
    case 'd': p = alloc_pages(4); free_pages(p, 4); free_pages(p, 4); break;
    case 'i': p = alloc_pages(4); free_pages(p + 1, 1); break;
    case 's': p = alloc_pages(4); free_pages(p, 2); break;
    case 'r': free_pages(pages + 128, 1); break;
    case 'o': free_pages(pages + CAPACITY, 1); break;
    case 'm': register_range(64, 64); break;
    case 'n': free_pages(NULL, 1); break;
    case 'u': free_pages((struct Page *)((char *)pages + 1), 1); break;
    case 'a': p = alloc_pages(1); set_page_ref(p, 1); free_pages(p, 1); break;
    case 'f': p = alloc_pages(1); free_pages(p, 0); break;
    default: assert(0);
    }
    panic("invalid operation was accepted");
}
int main(int argc, char **argv) {
    if (argc > 1) invalid_case(argv[1][0]);
    test_sizes();
    test_ranges();
    test_max_order();
    test_random();
    cprintf("HOST ALL PASS\n");
    return 0;
}
