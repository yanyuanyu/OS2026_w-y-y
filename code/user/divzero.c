#include <stdio.h>
#include <ulib.h>

int zero;

int
main(void) {
    long quotient;
    long dividend = 1;
    long divisor = zero;

    /* Test RV64 signed word division directly: DIVW by zero returns -1.
     * A C expression such as 1 / zero has undefined behavior, so its
     * generated instructions depend on the compiler and optimization.
     */
    asm volatile("divw %0, %1, %2"
                 : "=r" (quotient)
                 : "r" (dividend), "r" (divisor));
    cprintf("value is %d.\n", (int)quotient);
    panic("FAIL: T.T\n");
}

