// Stress test — 10,000 round trip swaps
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

static Context main_ctx;
static Context fiber_ctx;
static volatile int stress_count = 0;

static void fiber_func() {
    while (1) {
        stress_count++;
        swap_context(&fiber_ctx, &main_ctx);
    }
}

int main() {
    printf("[test_stress_swap]\n");

    static char stack[8192];
    fiber_ctx = {};
    fiber_ctx.rip = (void*)fiber_func;
    fiber_ctx.rsp = align_stack(stack, sizeof stack);

    const int N = 10000;
    for (int i = 0; i < N; i++) {
        swap_context(&main_ctx, &fiber_ctx);
    }

    if (stress_count == N) {
        printf("  PASS: 10,000 swaps completed without crash\n");
    } else {
        printf("  FAIL: expected %d swaps, got %d\n", N, stress_count);
        return 1;
    }

    return 0;
}
