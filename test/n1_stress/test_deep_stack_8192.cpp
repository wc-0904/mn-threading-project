// Each fiber does deep recursion to stress the 8192 byte stack
// 150 frames with volatile locals should fit in 8192 bytes
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 10
#define DEPTH 150

static int results[NUM_FIBERS];

int recursive_sum(int n) {
    if (n <= 0) return 0;
    volatile int val = n;
    return val + recursive_sum(n - 1);
}

void my_work(void *arg) {
    int id = *(int*)arg;
    results[id] = recursive_sum(DEPTH);
}

int main() {
    printf("[test_deep_stack_8192]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        results[i] = -1;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    int expected = (DEPTH * (DEPTH + 1)) / 2;
    int pass = 1;
    for (int i = 0; i < NUM_FIBERS; i++) {
        if (results[i] != expected) {
            printf("  FAIL: fiber %d result = %d, expected %d\n",
                i, results[i], expected);
            pass = 0;
        }
    }

    if (pass) {
        printf("  PASS: %d fibers each recursed %d deep, result = %d\n",
            NUM_FIBERS, DEPTH, expected);
    }

    return pass ? 0 : 1;
}
