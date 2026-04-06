// get_context + set_context — should loop exactly 5 times
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

int main() {
    printf("[test_get_set_loop]\n");

    volatile int count = 0;
    Context c;
    get_context(&c);

    count++;

    if (count < 5) {
        set_context(&c);
    }

    if (count == 5) {
        printf("  PASS: looped exactly 5 times\n");
    } else {
        printf("  FAIL: expected count=5, got %d\n", count);
        return 1;
    }

    return 0;
}
