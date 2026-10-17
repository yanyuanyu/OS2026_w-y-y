#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__
#include <defs.h>
/* Teaching SLUB: single CPU, 16-byte alignment, page-backed size classes. */
void slub_init(void);
void *slub_alloc(size_t size);
void slub_free(void *ptr);
void slub_check(void);
#endif
