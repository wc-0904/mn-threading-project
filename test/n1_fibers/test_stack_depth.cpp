// Fiber does recursive calls to test stack depth
// With a 4096 byte stack this tests whether we have enough room
// Uses a modest depth to avoid overflow
#include <stdio.h>
#include "fiber.h"

static int recursion_result = 0;

int recursive_sum(int n) {
    if (n <= 0) return 0;
    volatile int val = n;  // force stack usage
    return val + recursive_sum(n - 1);
}

void my_work(void *arg) {
    int depth = *(int*)arg;
    recursion_result = recursive_sum(depth);
}

int main() {
    printf("[test_stack_depth]\n");
    scheduler_init();

    // modest depth — 50 frames should fit in 4096 bytes
    int depth = 50;
    spawn(my_work, &depth);

    scheduler_run();

    int expected = (depth * (depth + 1)) / 2;
    if (recursion_result == expected) {
        printf("  PASS: recursive sum(%d) = %d\n", depth, recursion_result);
    } else {
        printf("  FAIL: expected %d, got %d\n", expected, recursion_result);
        return 1;
    }

    return 0;
}
