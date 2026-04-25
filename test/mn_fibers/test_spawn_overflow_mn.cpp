// Try to spawn more than MAX_FIBERS under M:N
// Verify spawn returns -1 when pool is full
#include <stdio.h>
#include <atomic>
#include "fiber.h"

static std::atomic<int> completed{0};

void my_work(void *arg) {
    yield();
    completed.fetch_add(1);
}

int main() {
    printf("[test_spawn_overflow_mn]\n");
    scheduler_init();

    int ids[MAX_FIBERS + 1];
    for (int i = 0; i <= MAX_FIBERS; i++) {
        ids[i] = i;
    }

    for (int i = 0; i < MAX_FIBERS; i++) {
        int ret = spawn(my_work, &ids[i]);
        if (ret != 0) {
            printf("  FAIL: spawn failed on fiber %d before pool full\n", i);
            return 1;
        }
    }

    int ret = spawn(my_work, &ids[MAX_FIBERS]);
    if (ret != -1) {
        printf("  FAIL: spawn returned %d instead of -1 when pool full\n", ret);
        return 1;
    }

    scheduler_run();

    if (completed.load() == MAX_FIBERS) {
        printf("  PASS: overflow detected, all %d fibers completed\n", MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d completions, got %d\n",
            MAX_FIBERS, completed.load());
        return 1;
    }

    return 0;
}
