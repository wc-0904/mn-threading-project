// Verify callee-saved registers are preserved across swap_context
// Fiber clobbers all callee-saved regs, main checks its values survived
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

static Context main_ctx;
static Context fiber_ctx;

static void fiber_func() {
    asm volatile(
        "movq $0xDEAD, %%rbx\n"
        "movq $0xCAFE, %%r12\n"
        "movq $0xBABE, %%r13\n"
        "movq $0xF00D, %%r14\n"
        "movq $0xD00D, %%r15\n"
        ::: "rbx", "r12", "r13", "r14", "r15"
    );
    swap_context(&fiber_ctx, &main_ctx);
}

int main() {
    printf("[test_register_preservation]\n");

    static char stack[8192];
    fiber_ctx = {};
    fiber_ctx.rip = (void*)fiber_func;
    fiber_ctx.rsp = align_stack(stack, sizeof stack);

    volatile uint64_t rbx_val, r12_val, r13_val, r14_val, r15_val;

    asm volatile(
        "movq $0x1111, %%rbx\n"
        "movq $0x3333, %%r12\n"
        "movq $0x4444, %%r13\n"
        "movq $0x5555, %%r14\n"
        "movq $0x6666, %%r15\n"
        ::: "rbx", "r12", "r13", "r14", "r15"
    );

    swap_context(&main_ctx, &fiber_ctx);

    asm volatile(
        "movq %%rbx, %0\n"
        "movq %%r12, %1\n"
        "movq %%r13, %2\n"
        "movq %%r14, %3\n"
        "movq %%r15, %4\n"
        : "=r"(rbx_val),
          "=r"(r12_val), "=r"(r13_val),
          "=r"(r14_val), "=r"(r15_val)
    );

    if (rbx_val == 0x1111 &&
        r12_val == 0x3333 && r13_val == 0x4444 &&
        r14_val == 0x5555 && r15_val == 0x6666) {
        printf("  PASS: all callee-saved registers preserved\n");
    } else {
        printf("  FAIL: registers were clobbered\n");
        printf("  rbx=0x%lx r12=0x%lx r13=0x%lx r14=0x%lx r15=0x%lx\n",
            rbx_val, r12_val, r13_val, r14_val, r15_val);
        return 1;
    }

    return 0;
}