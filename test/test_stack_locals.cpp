// Verify local variables on fiber's stack survive a swap
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

static Context main_ctx;
static Context fiber_ctx;
static volatile int locals_ok = 0;

static void fiber_func() {
    volatile int a = 111;
    volatile int b = 222;
    volatile int c = 333;

    // swap out — locals live on fiber's stack
    swap_context(&fiber_ctx, &main_ctx);

    // swap back in — check locals survived
    if (a == 111 && b == 222 && c == 333) {
        locals_ok = 1;
    }

    swap_context(&fiber_ctx, &main_ctx);
}

int main() {
    printf("[test_stack_locals]\n");

    static char stack[8192];
    fiber_ctx = {};
    fiber_ctx.rip = (void*)fiber_func;
    fiber_ctx.rsp = align_stack(stack, sizeof stack);

    // first swap: fiber sets locals, swaps back
    swap_context(&main_ctx, &fiber_ctx);
    // second swap: fiber checks locals, swaps back
    swap_context(&main_ctx, &fiber_ctx);

    if (locals_ok) {
        printf("  PASS: fiber stack locals preserved across swap\n");
    } else {
        printf("  FAIL: locals were corrupted\n");
        return 1;
    }

    return 0;
}
