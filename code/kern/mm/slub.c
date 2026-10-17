#include <slub.h>
#include <pmm.h>
#include <string.h>

/* lab2 Challenge: 王优2413681 严苑毓2413682 杨思远2413636
 * Simplified single-CPU SLUB: page-backed caches plus a large-object path.
 * Not Linux SLUB: no per-CPU caches, NUMA, constructors or synchronization. */
#define SLUB_MAGIC 0x534c55424c414232ULL
#define SLUB_CLASSES 8

struct slab {
    uint64_t magic;
    size_t class_index;
    size_t inuse;
    size_t capacity;
    size_t page_count;
    struct slab *next;
    void *free_head;
    uint64_t allocated[4];
};

static struct slab *caches[SLUB_CLASSES];

static size_t data_offset(void) { return ROUNDUP(sizeof(struct slab), 16); }
static size_t class_size(size_t index) { return (size_t)16 << index; }
static void *page_address(struct Page *p) {
    return (void *)(page2pa(p) + va_pa_offset);
}

void slub_init(void) {
    for (size_t i = 0; i < SLUB_CLASSES; i++) {
        assert(caches[i] == NULL);
    }
}

void *slub_alloc(size_t size) {
    if (size == 0) return NULL;
    size_t index = 0;
    while (index < SLUB_CLASSES && class_size(index) < size) index++;
    size_t offset = data_offset();
    if (index == SLUB_CLASSES) {
        if (size > ~(size_t)0 - offset - (PGSIZE - 1)) return NULL;
        size_t count = (size + offset + PGSIZE - 1) / PGSIZE;
        if (count > nr_free_pages()) return NULL;
        struct Page *p = alloc_pages(count);
        if (p == NULL) return NULL;
        struct slab *s = page_address(p);
        memset(s, 0, sizeof(*s));
        s->magic = SLUB_MAGIC;
        s->class_index = SLUB_CLASSES;
        s->page_count = count;
        s->inuse = 1;
        return (char *)s + offset;
    }

    struct slab *s = caches[index];
    while (s != NULL && s->free_head == NULL) s = s->next;
    if (s == NULL) {
        struct Page *p = alloc_page();
        if (p == NULL) return NULL;
        s = page_address(p);
        memset(s, 0, sizeof(*s));
        s->magic = SLUB_MAGIC;
        s->class_index = index;
        s->page_count = 1;
        s->capacity = (PGSIZE - offset) / class_size(index);
        assert(s->capacity > 0 && s->capacity <= 256);
        for (size_t i = 0; i < s->capacity; i++) {
            void *obj = (char *)s + offset + i * class_size(index);
            *(void **)obj = s->free_head;
            s->free_head = obj;
        }
        s->next = caches[index];
        caches[index] = s;
    }
    void *obj = s->free_head;
    s->free_head = *(void **)obj;
    size_t slot = ((uintptr_t)obj - (uintptr_t)s - offset) / class_size(index);
    assert((s->allocated[slot / 64] & (1ULL << (slot % 64))) == 0);
    s->allocated[slot / 64] |= 1ULL << (slot % 64);
    s->inuse++;
    return obj;
}

void slub_free(void *ptr) {
    if (ptr == NULL) return;
    struct slab *s = (struct slab *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    assert(s->magic == SLUB_MAGIC && s->inuse > 0);
    struct Page *base = pa2page(PADDR(s));
    size_t offset = data_offset();
    if (s->class_index == SLUB_CLASSES) {
        assert(ptr == (char *)s + offset);
        size_t count = s->page_count;
        s->magic = 0;
        free_pages(base, count);
        return;
    }
    assert(s->class_index < SLUB_CLASSES);
    size_t index = s->class_index;
    uintptr_t delta = (uintptr_t)ptr - (uintptr_t)s;
    assert(delta >= offset && delta < offset + s->capacity * class_size(index));
    assert((delta - offset) % class_size(index) == 0);
    size_t slot = (delta - offset) / class_size(index);
    uint64_t mask = 1ULL << (slot % 64);
    assert((s->allocated[slot / 64] & mask) != 0);
    s->allocated[slot / 64] &= ~mask;
    *(void **)ptr = s->free_head;
    s->free_head = ptr;
    if (--s->inuse == 0) {
        struct slab **link = &caches[index];
        while (*link != s) {
            assert(*link != NULL);
            link = &(*link)->next;
        }
        *link = s->next;
        s->magic = 0;
        free_page(base);
    }
}
