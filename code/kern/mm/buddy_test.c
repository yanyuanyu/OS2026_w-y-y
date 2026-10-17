/* Kernel-only tests: direct-map content writes plus complete exhaustion. */
#include <buddy_pmm.h>
#include <stdio.h>

static size_t actual_pages(size_t n) {
    size_t actual = 1;
    while (actual < n) actual <<= 1;
    return actual;
}
static volatile unsigned char *page_data(struct Page *p) {
    return (volatile unsigned char *)(page2pa(p) + va_pa_offset);
}

void buddy_check(void) {
    extern void c_memory_probe(void);
    extern volatile uintptr_t c_probe_word;
    static const size_t sizes[] = {1, 2, 3, 5, 8, 9, 16, 31, 33};
    struct Page *allocated[sizeof(sizes) / sizeof(sizes[0])];
    size_t before = nr_free_pages(), used = 0;
    assert(before >= 512);
    buddy_validate();
    assert(alloc_pages(before + 1) == NULL);
    assert(alloc_pages((size_t)-1) == NULL);
    assert(nr_free_pages() == before);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t actual = actual_pages(sizes[i]);
        struct Page *p = allocated[i] = alloc_pages(sizes[i]);
        assert(p != NULL && !PageProperty(p) && p->property == actual);
        assert(((size_t)(p - pages) & (actual - 1)) == 0);
        for (size_t j = 0; j < i; ++j)
            assert(p + actual <= allocated[j] ||
                   allocated[j] + actual_pages(sizes[j]) <= p);
        volatile unsigned char *data = page_data(p);
        for (size_t offset = 0; offset < actual * PGSIZE; offset += PGSIZE) {
            data[offset] = (unsigned char)(i + 1);
            data[offset + PGSIZE - 1] = (unsigned char)(0xa0 + i);
        }
        used += actual;
        assert(nr_free_pages() == before - used);
        buddy_validate();
    }
    /* Check earlier allocations after later writes, not only immediately. */
    for (size_t i = sizeof(sizes) / sizeof(sizes[0]); i-- > 0;) {
        volatile unsigned char *data = page_data(allocated[i]);
        for (size_t offset = 0; offset < actual_pages(sizes[i]) * PGSIZE;
             offset += PGSIZE) {
            assert(data[offset] == (unsigned char)(i + 1));
            assert(data[offset + PGSIZE - 1] == (unsigned char)(0xa0 + i));
        }
        free_pages(allocated[i], sizes[i]);
        used -= actual_pages(sizes[i]);
        assert(nr_free_pages() == before - used);
        buddy_validate();
    }
    cprintf("C-BUDDY rounding/alignment/non-overlap/content/accounting PASS\n");

    list_entry_t held;
    list_init(&held);
    size_t exhausted = 0;
    struct Page *p;
    while ((p = alloc_pages(1)) != NULL) {
        assert(p->property == 1);
        list_add(&held, &p->page_link);
        page_data(p)[0] = 0x5a;
        ++exhausted;
        assert(exhausted <= before);
    }
    assert(exhausted == before && nr_free_pages() == 0);
    assert(alloc_pages(2) == NULL);
    buddy_validate();
    while (!list_empty(&held)) {
        p = le2page(list_next(&held), page_link);
        list_del_init(&p->page_link);
        assert(page_data(p)[0] == 0x5a);
        free_pages(p, 1);
    }
    assert(nr_free_pages() == before);
    buddy_validate(); /* includes absence of any unmerged same-order buddies */
    cprintf("C-BUDDY exhausted/restored %lu pages PASS\n", (unsigned long)exhausted);

    for (int i = 0; i < 100; ++i) {
        p = alloc_pages(64);
        assert(p != NULL && nr_free_pages() == before - 64);
        free_pages(p, 64);
        assert(nr_free_pages() == before);
        buddy_validate();
    }
    cprintf("C-BUDDY split/merge/repeat PASS\n");
    cprintf("C-BUDDY ALL PASS\n");
    c_memory_probe();
    assert(c_probe_word == 0x8877665544332211ULL);
    cprintf("C-PROBE store pair PASS\n");
}
