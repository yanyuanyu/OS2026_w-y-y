#include <slub.h>
#include <pmm.h>
#include <stdio.h>

static void check_pattern(void *ptr, size_t n, unsigned char tag) {
    unsigned char *p = ptr;
    for (size_t i = 0; i < n; i++) p[i] = tag;
}

void slub_check(void) {
    size_t before = nr_free_pages();
    const size_t sizes[] = {1, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 255, 256, 257, 511, 512, 513, 1023,
        1024, 1025, 2047, 2048, 2049, 4096, 9000};
    void *objects[26];
    assert(slub_alloc(0) == NULL);
    assert(slub_alloc(~(size_t)0) == NULL);
    assert(slub_alloc((before + 1) * PGSIZE) == NULL);
    assert(nr_free_pages() == before);
    slub_free(NULL);
    for (unsigned int i = 0; i < 26; i++) {
        objects[i] = slub_alloc(sizes[i]);
        assert(objects[i] != NULL && ((uintptr_t)objects[i] & 15) == 0);
        for (unsigned int j = 0; j < i; j++) {
            assert((uintptr_t)objects[i] + sizes[i] <= (uintptr_t)objects[j] ||
                   (uintptr_t)objects[j] + sizes[j] <= (uintptr_t)objects[i]);
        }
        check_pattern(objects[i], sizes[i], (unsigned char)(i + 1));
    }
    for (unsigned int i = 0; i < 26; i++) {
        unsigned char *p = objects[i];
        for (size_t j = 0; j < sizes[i]; j++) assert(p[j] == i + 1);
    }
    for (unsigned int i = 26; i > 0; i--) slub_free(objects[i - 1]);
    assert(nr_free_pages() == before);

    /* 140 objects exceed two 64-byte slabs; keep one object per page alive
     * while verifying freed slots are reused without acquiring new pages. */
    void *small[140];
    for (unsigned int i = 0; i < 140; i++) {
        small[i] = slub_alloc(64);
        assert(small[i] != NULL);
        check_pattern(small[i], 64, (unsigned char)i);
    }
    size_t occupied = nr_free_pages();
    assert(occupied < before - 1);
    for (unsigned int i = 0; i < 140; i += 2) slub_free(small[i]);
    assert(nr_free_pages() == occupied);
    for (unsigned int i = 0; i < 140; i += 2) {
        small[i] = slub_alloc(64);
        assert(small[i] != NULL);
    }
    assert(nr_free_pages() == occupied);
    for (unsigned int i = 1; i < 140; i += 2) {
        unsigned char *p = small[i];
        for (unsigned int j = 0; j < 64; j++) assert(p[j] == (unsigned char)i);
    }
    for (unsigned int i = 0; i < 140; i++) slub_free(small[i]);
    assert(nr_free_pages() == before);
    cprintf("slub_check: size boundaries, alignment, data, reuse, cross-page, large, reclaim PASS\n");
    cprintf("slub_check() succeeded! free pages restored: %lu\n", (unsigned long)before);
}
