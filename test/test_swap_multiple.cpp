// swap_context — 5 round trips between main and fiber
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

static Context main_ctx;
static Context fiber_ctx;
static volatile int swap_count = 0;

static void fiber_func() {
    for (int i = 0; i < 5; i++) {
        swap_count++;
        printf("  fiber iteration %d\n", i);
        swap_context(&fiber_ctx, &main_ctx);
    }
    swap_context(&fiber_ctx, &main_ctx);
}

int main() {
    printf("[test_swap_multiple]\n");

    static char stack[8192];
    void *sp = align_stack(stack, sizeof stack);

    fiber_ctx = {};
    fiber_ctx.rip = (void*)fiber_func;
    fiber_ctx.rsp = sp;

    for (int i = 0; i < 5; i++) {
        printf("  main iteration %d\n", i);
        swap_context(&main_ctx, &fiber_ctx);
    }
    swap_context(&main_ctx, &fiber_ctx);

    if (swap_count == 5) {
        printf("  PASS: 5 round trips completed\n");
    } else {
        printf("  FAIL: expected 5 swaps, got %d\n", swap_count);
        return 1;
    }

    return 0;
}
