// get_context + set_context — should print exactly twice
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

int main() {
    printf("[test_get_set_basic]\n");

    volatile int count = 0;
    Context c;
    get_context(&c);

    count++;
    printf("  iteration %d\n", count);

    if (count == 1) {
        set_context(&c);
    }

    if (count == 2) {
        printf("  PASS\n");
    } else {
        printf("  FAIL: expected count=2, got %d\n", count);
        return 1;
    }

    return 0;
}
