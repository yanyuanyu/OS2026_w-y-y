/* Isolate libc headers from ucore's freestanding typedefs. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
int cprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
void __panic(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "PANIC %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(99);
}
