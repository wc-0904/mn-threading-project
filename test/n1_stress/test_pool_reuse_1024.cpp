// Spawn MAX_FIBERS, run them, repeat 10 times
// Tests that pool reuse works correctly at scale
#include <stdio.h>
#include "fiber.h"

#define ROUNDS 10

static int total_completed = 0;

void my_work(void *arg) {
    total_completed++;
}

int main() {
    printf("[test_pool_reuse_1024]\n");

    for (int r = 0; r < ROUNDS; r++) {
        scheduler_init();

        int ids[MAX_FIBERS];
        for (int i = 0; i < MAX_FIBERS; i++) {
            ids[i] = i;
            int ret = spawn(my_work, &ids[i]);
            if (ret != 0) {
                printf("  FAIL: round %d, could not spawn fiber %d\n", r, i);
                return 1;
            }
        }

        scheduler_run();
    }

    int expected = MAX_FIBERS * ROUNDS;
    if (total_completed == expected) {
        printf("  PASS: %d rounds of %d fibers = %d total completions\n",
            ROUNDS, MAX_FIBERS, total_completed);
    } else {
        printf("  FAIL: expected %d completions, got %d\n", expected, total_completed);
        return 1;
    }

    return 0;
}
