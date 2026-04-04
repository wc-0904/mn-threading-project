// swap_context — two fibers, three-way swap
// main -> A -> B -> A -> main
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

static Context main_ctx;
static Context fiber_a_ctx;
static Context fiber_b_ctx;
static volatile int order[4];
static volatile int order_idx = 0;

static void fiber_a_func() {
    order[order_idx++] = 1;
    printf("  fiber A -> swapping to fiber B\n");
    swap_context(&fiber_a_ctx, &fiber_b_ctx);

    order[order_idx++] = 3;
    printf("  fiber A -> swapping back to main\n");
    swap_context(&fiber_a_ctx, &main_ctx);
}

static void fiber_b_func() {
    order[order_idx++] = 2;
    printf("  fiber B -> swapping to fiber A\n");
    swap_context(&fiber_b_ctx, &fiber_a_ctx);
}

int main() {
    printf("[test_swap_two_fibers]\n");

    static char stack_a[8192];
    static char stack_b[8192];

    fiber_a_ctx = {};
    fiber_a_ctx.rip = (void*)fiber_a_func;
    fiber_a_ctx.rsp = align_stack(stack_a, sizeof stack_a);

    fiber_b_ctx = {};
    fiber_b_ctx.rip = (void*)fiber_b_func;
    fiber_b_ctx.rsp = align_stack(stack_b, sizeof stack_b);

    swap_context(&main_ctx, &fiber_a_ctx);

    if (order_idx == 3 &&
        order[0] == 1 &&
        order[1] == 2 &&
        order[2] == 3) {
        printf("  PASS: three-way swap order correct (A->B->A->main)\n");
    } else {
        printf("  FAIL: wrong execution order\n");
        return 1;
    }

    return 0;
}
