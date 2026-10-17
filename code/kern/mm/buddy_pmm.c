/* Independent C-part implementation: rounded allocations, same-order buddies. */
#include <buddy_pmm.h>

/* PG_reserved/PG_property remain framework-owned; no Page ABI changes. */
#define BUDDY_ALLOCATED (1UL << 2)
static list_entry_t order_lists[BUDDY_MAX_ORDER + 1];
static size_t free_count;

static size_t span(unsigned order) { return (size_t)1 << order; }

static size_t checked_index(struct Page *p, size_t n) {
    /* Validate integers before pointer subtraction, including foreign pointers. */
    uintptr_t address = (uintptr_t)p, start = (uintptr_t)pages;
    assert(p != NULL && address >= start);
    assert((address - start) % sizeof(struct Page) == 0);
    size_t index = (address - start) / sizeof(struct Page);
    assert(npage >= nbase && index < npage - nbase);
    assert(n > 0 && n <= npage - nbase - index);
    return index;
}

static unsigned request_order(size_t n) {
    unsigned order = 0;
    while (order < BUDDY_MAX_ORDER && span(order) < n) ++order;
    return order;
}

static void buddy_init(void) {
    for (unsigned order = 0; order <= BUDDY_MAX_ORDER; ++order)
        list_init(&order_lists[order]);
    free_count = 0;
}

static void link_block(size_t index, unsigned order) {
    struct Page *p = pages + index;
    p->property = span(order);
    SetPageProperty(p);
    list_add(&order_lists[order], &p->page_link);
}

/* Called only for pages already returned to the free state; no count update. */
static void coalesce(size_t index, unsigned order) {
    while (order < BUDDY_MAX_ORDER) {
        size_t partner = index ^ span(order);
        if (partner >= npage - nbase ||
            span(order) > npage - nbase - partner) break;
        struct Page *other = pages + partner;
        if (!PageProperty(other) || other->property != span(order)) break;
        assert(!PageReserved(other) && !(other->flags & BUDDY_ALLOCATED));
        list_del_init(&other->page_link);
        ClearPageProperty(other);
        other->property = 0;
        if (partner < index) index = partner;
        ++order;
    }
    link_block(index, order);
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    size_t index = checked_index(base, n);
    /* Check the entire range before changing anything. Reject overlapping maps. */
    for (size_t i = 0; i < n; ++i) assert(PageReserved(base + i));
    for (size_t i = 0; i < n; ++i) {
        base[i].flags = 0;
        base[i].property = 0;
        set_page_ref(base + i, 0);
        list_init(&base[i].page_link);
    }
    free_count += n;
    while (n) {
        unsigned order = BUDDY_MAX_ORDER;
        while (span(order) > n || (index & (span(order) - 1))) --order;
        size_t count = span(order);
        coalesce(index, order);
        index += count;
        n -= count;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    /* Bound n before order calculation, so SIZE_MAX never overflows rounding. */
    if (n > span(BUDDY_MAX_ORDER) || n > free_count) return NULL;
    unsigned wanted = request_order(n), order = wanted;
    while (order <= BUDDY_MAX_ORDER && list_empty(&order_lists[order])) ++order;
    if (order > BUDDY_MAX_ORDER) return NULL;

    struct Page *block = le2page(list_next(&order_lists[order]), page_link);
    size_t index = block - pages;
    list_del_init(&block->page_link);
    ClearPageProperty(block);
    block->property = 0;
    while (order > wanted) {
        --order;
        link_block(index + span(order), order);
    }
    size_t actual = span(wanted);
    for (size_t i = 0; i < actual; ++i) {
        assert(!PageReserved(block + i) && !PageProperty(block + i));
        assert(!(block[i].flags & BUDDY_ALLOCATED));
        block[i].flags = BUDDY_ALLOCATED;
        block[i].property = 0;
        set_page_ref(block + i, 0);
    }
    /* Only allocation heads carry size. This also rejects interior frees. */
    block->property = actual;
    free_count -= actual;
    return block;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    size_t index = checked_index(base, n);
    assert(n <= span(BUDDY_MAX_ORDER));
    unsigned order = request_order(n);
    size_t actual = span(order);
    assert(actual <= npage - nbase - index);
    assert((index & (actual - 1)) == 0);
    assert(base->property == actual && !PageProperty(base));
    for (size_t i = 0; i < actual; ++i) {
        assert(!PageReserved(base + i) && !PageProperty(base + i));
        assert(base[i].flags & BUDDY_ALLOCATED);
        assert(page_ref(base + i) == 0);
    }
    for (size_t i = 0; i < actual; ++i) {
        base[i].flags = 0;
        base[i].property = 0;
    }
    free_count += actual;
    coalesce(index, order);
}

static size_t buddy_nr_free_pages(void) { return free_count; }

/* Expensive structural checks used by tests/debugging, not the fast path. */
void buddy_validate(void) {
    size_t total = 0, capacity = npage - nbase;
    for (unsigned order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        list_entry_t *head = &order_lists[order];
        size_t visited = 0;
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            assert(++visited <= capacity); /* detect cycles */
            assert(list_next(list_prev(le)) == le && list_prev(list_next(le)) == le);
            struct Page *p = le2page(le, page_link);
            size_t index = checked_index(p, span(order));
            assert((index & (span(order) - 1)) == 0);
            assert(PageProperty(p) && p->property == span(order));
            for (size_t i = 0; i < span(order); ++i) {
                assert(!PageReserved(p + i) && !(p[i].flags & BUDDY_ALLOCATED));
                if (i) assert(!PageProperty(p + i) && p[i].property == 0);
            }
            size_t partner = index ^ span(order);
            if (order < BUDDY_MAX_ORDER && partner < capacity)
                assert(!PageProperty(pages + partner) ||
                       pages[partner].property != span(order));
            total += span(order);
            assert(total <= capacity);
        }
    }
    assert(total == free_count);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
