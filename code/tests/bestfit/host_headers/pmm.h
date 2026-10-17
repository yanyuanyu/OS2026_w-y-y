#ifndef LAB2_HOST_PMM_H
#define LAB2_HOST_PMM_H
// Host adapter: no allocation algorithm is implemented here.
// Page and list definitions come from the original kernel headers.
#include <assert.h>
#include <defs.h>
#include <memlayout.h>
#define PGSIZE 4096UL
#define PGSHIFT 12
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *, size_t);
    struct Page *(*alloc_pages)(size_t);
    void (*free_pages)(struct Page *, size_t);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern const struct pmm_manager *pmm_manager;
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
int cprintf(const char *fmt, ...);
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages(p, 1)
static inline int page_ref(struct Page *p) { return p->ref; }
static inline void set_page_ref(struct Page *p, int value) { p->ref = value; }
static inline uintptr_t page2pa(struct Page *p) {
    return (uintptr_t)(p - pages + nbase) << PGSHIFT;
}
#endif
