#ifndef LAB2_HOST_DEFS_H
#define LAB2_HOST_DEFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define to_struct(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
