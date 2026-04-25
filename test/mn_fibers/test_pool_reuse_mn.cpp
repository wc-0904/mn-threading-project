// Run multiple batches of MAX_FIBERS, each batch must
// complete before the next starts. Tests pool reuse
// under M:N with work stealing
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define ROUNDS 5

static std::atomic<int> total_completed{0};

void my_work(void *arg) {
    total_completed.fetch_add(1);
}

int main() {
    printf("[test_pool_reuse_mn]\n");

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
        printf("  round %d complete (%d total)\n", r, total_completed.load());
    }

    int expected = MAX_FIBERS * ROUNDS;
    if (total_completed.load() == expected) {
        printf("  PASS: %d rounds of %d fibers = %d total\n",
            ROUNDS, MAX_FIBERS, total_completed.load());
    } else {
        printf("  FAIL: expected %d, got %d\n", expected, total_completed.load());
        return 1;
    }

    return 0;
}
