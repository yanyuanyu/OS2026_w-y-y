#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pmm.h>
// Compile the real submission implementation, including unchanged teacher tests.
#include "best_fit_pmm.c"

#define NPAGES 256
static struct Page pool[NPAGES];
// Independent oracle: 0 = available, 1 = allocated, 2 = reserved.
static unsigned char state[NPAGES];
struct Page *pages = pool;
const size_t nbase = 0x80000000UL / PGSIZE;
size_t npage = 0x80000000UL / PGSIZE + NPAGES;
const struct pmm_manager *pmm_manager = &best_fit_pmm_manager;

int cprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vprintf(fmt, ap);
    va_end(ap);
    return result;
}
struct Page *alloc_pages(size_t n) { return pmm_manager->alloc_pages(n); }
void free_pages(struct Page *p, size_t n) { pmm_manager->free_pages(p, n); }
size_t nr_free_pages(void) { return pmm_manager->nr_free_pages(); }

static void check_state(void) {
    unsigned char covered[NPAGES] = {0};
    size_t total = 0, blocks = 0;
    struct Page *previous = NULL;
    assert(free_list.next->prev == &free_list);
    assert(free_list.prev->next == &free_list);
    for (list_entry_t *le = list_next(&free_list); le != &free_list; le = list_next(le)) {
        assert(++blocks <= NPAGES);
        assert(le->next->prev == le && le->prev->next == le);
        struct Page *p = le2page(le, page_link);
        assert(p >= pool && p < pool + NPAGES);
        size_t start = (size_t)(p - pool), n = p->property;
        assert(n > 0 && start + n <= NPAGES && PageProperty(p));
        if (previous != NULL) {
            assert(previous < p);
            assert(previous + previous->property < p); // Adjacent blocks must coalesce.
        }
        for (size_t j = 0; j < n; j++) {
            assert(state[start + j] == 0 && covered[start + j] == 0);
            covered[start + j] = 1;
            assert(!PageReserved(p + j) && page_ref(p + j) == 0);
            if (j > 0) assert(!PageProperty(p + j) && p[j].property == 0);
        }
        total += n;
        previous = p;
    }
    size_t expected = 0;
    for (size_t i = 0; i < NPAGES; i++) {
        if (state[i] == 0) { expected++; assert(covered[i]); }
        else {
            assert(!covered[i] && !PageProperty(pool + i));
            assert((bool)PageReserved(pool + i) == (state[i] == 2));
        }
    }
    assert(total == expected && total == nr_free_pages());
}

static void reset_pool(void) {
    memset(pool, 0, sizeof(pool));
    memset(state, 2, sizeof(state));
    for (size_t i = 0; i < NPAGES; i++) SetPageReserved(pool + i);
    pmm_manager->init();
    check_state();
}
static void register_range(size_t start, size_t n) {
    assert(start + n <= NPAGES);
    for (size_t j = 0; j < n; j++) assert(state[start + j] == 2);
    pmm_manager->init_memmap(pool + start, n);
    for (size_t j = 0; j < n; j++) state[start + j] = 0;
    check_state();
}
// Compute Best-Fit from the independent occupancy bitmap, not the free list.
static struct Page *expected_allocation(size_t n) {
    size_t best_start = NPAGES, best_len = NPAGES + 1;
    for (size_t i = 0; i < NPAGES;) {
        if (state[i] != 0) { i++; continue; }
        size_t begin = i;
        while (i < NPAGES && state[i] == 0) i++;
        size_t len = i - begin;
        if (len >= n && len < best_len) { best_start = begin; best_len = len; }
    }
    return best_start == NPAGES ? NULL : pool + best_start;
}
static struct Page *allocate_checked(size_t n) {
    struct Page *want = expected_allocation(n);
    struct Page saved_pool[NPAGES];
    memcpy(saved_pool, pool, sizeof(pool));
    list_entry_t saved_list = free_list;
    size_t saved_free = nr_free_pages();
    struct Page *p = alloc_pages(n);
    assert(p == want);
    if (p == NULL) {
        assert(memcmp(saved_pool, pool, sizeof(pool)) == 0);
        assert(memcmp(&saved_list, &free_list, sizeof(free_list)) == 0);
        assert(nr_free_pages() == saved_free);
    } else {
        size_t start = p - pool;
        for (size_t i = 0; i < n; i++) {
            assert(state[start + i] == 0);
            state[start + i] = 1;
        }
    }
    check_state();
    return p;
}
static void release_checked(struct Page *p, size_t n) {
    size_t start = p - pool;
    assert(start + n <= NPAGES);
    for (size_t i = 0; i < n; i++) assert(state[start + i] == 1);
    free_pages(p, n);
    for (size_t i = 0; i < n; i++) state[start + i] = 0;
    check_state();
}
static void pass(const char *name) { printf("PASS %s\n", name); }

static void deterministic_tests(void) {
    reset_pool(); register_range(0, NPAGES);
    basic_check(); check_state();
    pass("H01 unchanged teacher basic_check");
    best_fit_check(); check_state();
    pass("H02 unchanged teacher best_fit_check");

    reset_pool(); register_range(0, 8); register_range(16, 4); register_range(32, 6);
    struct Page *p = allocate_checked(3);
    assert(p == pool + 16 && pool[19].property == 1);
    release_checked(p, 3);
    pass("H03 minimum suitable block and split (8,4,6 -> request 3 selects 4)");

    p = allocate_checked(4); assert(p == pool + 16);
    release_checked(p, 4);
    pass("H04 exact fit removes only the selected block");

    reset_pool(); register_range(48, 4); register_range(8, 4);
    p = allocate_checked(2); assert(p == pool + 8);
    release_checked(p, 2);
    pass("H05 equal-size tie selects the lower address");

    reset_pool(); register_range(0, 4); register_range(16, 4);
    register_range(4, 4); register_range(8, 8);
    assert(pool[0].property == 20);
    pass("H06 multiple registrations: tail insertion and two-sided coalescing");

    reset_pool(); register_range(0, 12); p = allocate_checked(12);
    release_checked(p, 2); release_checked(p + 8, 2);
    assert(pool[8].page_link.prev == &pool[0].page_link);
    pass("H07 high-address free preserves tail backward link");
    release_checked(p + 4, 2); release_checked(p + 2, 2);
    release_checked(p + 6, 2); release_checked(p + 10, 2);
    assert(pool[0].property == 12);
    pass("H08 middle free joins predecessor and successor");

    reset_pool(); register_range(0, 16);
    p = allocate_checked(3); struct Page *q = allocate_checked(5);
    release_checked(p, 3); release_checked(q, 5);
    assert(nr_free_pages() == 16 && pool[0].property == 16);
    pass("H09 split allocations fully restore the original block");

    reset_pool(); register_range(0, 3); register_range(16, 3);
    assert(allocate_checked(7) == NULL);
    assert(allocate_checked(4) == NULL);
    pass("H10 shortage and fragmentation failures leave all state unchanged");

    reset_pool(); register_range(12, 1);
    p = allocate_checked(1); assert(p == pool + 12 && nr_free_pages() == 0);
    assert(allocate_checked(1) == NULL);
    release_checked(p, 1);
    pass("H11 single-page exhaustion, recovery and reserved-region protection");
}

struct allocation { struct Page *p; size_t n; };
static uint32_t next_random(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s;
}
static void randomized_test(uint32_t seed) {
    struct allocation live[NPAGES]; size_t count = 0;
    uint32_t rng = seed;
    reset_pool(); register_range(0, 80); register_range(96, 96); register_range(208, 48);
    size_t initial = nr_free_pages();
    for (size_t step = 0; step < 10000; step++) {
        if (count > 0 && next_random(&rng) % 100 < 45) {
            size_t slot = next_random(&rng) % count;
            release_checked(live[slot].p, live[slot].n);
            live[slot] = live[--count];
        } else {
            size_t n = 1 + next_random(&rng) % 32;
            struct Page *p = allocate_checked(n);
            if (p != NULL) { assert(count < NPAGES); live[count++] = (struct allocation){p, n}; }
        }
    }
    while (count > 0) { count--; release_checked(live[count].p, live[count].n); }
    assert(nr_free_pages() == initial);
    printf("PASS random seed=0x%08x operations=10000 (independent bitmap oracle)\n", seed);
}
int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    puts("Lab2 B: real Best-Fit C implementation / host verification");
    if (argc == 2 && strcmp(argv[1], "--tail-only") == 0) {
        reset_pool(); register_range(0, 12);
        struct Page *p = allocate_checked(12);
        release_checked(p, 2);
        puts("REGRESSION: free a higher-address block after the existing low block");
        release_checked(p + 8, 2);
        assert(pool[8].page_link.prev == &pool[0].page_link);
        pass("tail-insertion backward-link regression");
        return 0;
    }
    deterministic_tests();
    randomized_test(0x02413682); randomized_test(0x20261014); randomized_test(0xdeadbeef);
    puts("HOST RESULT: 11 deterministic tests + 30000 randomized operations PASS");
    return 0;
}
