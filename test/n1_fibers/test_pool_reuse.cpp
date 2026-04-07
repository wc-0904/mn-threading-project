// Spawn MAX_FIBERS, let them all complete, then spawn MAX_FIBERS again
// Verifies that pool slots are properly freed and reusable
#include <stdio.h>
#include "fiber.h"

static int batch1_done = 0;
static int batch2_done = 0;

void batch1_work(void *arg) {
    batch1_done++;
}

void batch2_work(void *arg) {
    batch2_done++;
}

int main() {
    printf("[test_pool_reuse]\n");
    scheduler_init();

    // first batch — fill the pool
    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        int ret = spawn(batch1_work, &ids[i]);
        if (ret != 0) {
            printf("  FAIL: could not spawn fiber %d in batch 1\n", i);
            return 1;
        }
    }

    scheduler_run();

    if (batch1_done != MAX_FIBERS) {
        printf("  FAIL: batch 1 completed %d, expected %d\n", batch1_done, MAX_FIBERS);
        return 1;
    }

    // second batch — should reuse freed slots
    // need to re-init scheduler since the queue is empty
    scheduler_init();

    for (int i = 0; i < MAX_FIBERS; i++) {
        int ret = spawn(batch2_work, &ids[i]);
        if (ret != 0) {
            printf("  FAIL: could not spawn fiber %d in batch 2 (pool not freed?)\n", i);
            return 1;
        }
    }

    scheduler_run();

    if (batch2_done == MAX_FIBERS) {
        printf("  PASS: pool fully reused across two batches of %d fibers\n", MAX_FIBERS);
    } else {
        printf("  FAIL: batch 2 completed %d, expected %d\n", batch2_done, MAX_FIBERS);
        return 1;
    }

    return 0;
}
