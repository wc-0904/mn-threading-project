// swap_context — basic swap to fiber and back
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

static Context main_ctx;
static Context fiber_ctx;
static volatile int fiber_ran = 0;

static void fiber_func() {
    printf("  inside fiber\n");
    fiber_ran = 1;
    swap_context(&fiber_ctx, &main_ctx);
}

int main() {
    printf("[test_swap_basic]\n");

    static char stack[8192];
    void *sp = align_stack(stack, sizeof stack);

    fiber_ctx = {};
    fiber_ctx.rip = (void*)fiber_func;
    fiber_ctx.rsp = sp;

    swap_context(&main_ctx, &fiber_ctx);

    if (fiber_ran == 1) {
        printf("  PASS: swapped to fiber and back\n");
    } else {
        printf("  FAIL: fiber did not run\n");
        return 1;
    }

    return 0;
}
