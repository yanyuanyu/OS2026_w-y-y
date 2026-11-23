#ifndef __KERN_MM_KMALLOC_H__
#define __KERN_MM_KMALLOC_H__

#include <defs.h>

#define KMALLOC_MAX_ORDER 10

void kmalloc_init(void);
size_t slob_reclaim(void);

void *kmalloc(size_t n);
void kfree(void *objp);

size_t kallocated(void);

#endif /* !__KERN_MM_KMALLOC_H__ */
