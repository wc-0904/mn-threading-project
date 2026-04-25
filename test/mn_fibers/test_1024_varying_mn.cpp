// 1024 fibers where each yields (id % 50) times
// Creates massive imbalance — some workers finish fast,
// others have fibers yielding 49 times
// Workers must steal to balance load
#include <stdio.h>
#include <atomic>
#include "fiber.h"

static std::atomic<int> completed{0};

void my_work(void *arg) {
    int id = *(int*)arg;
    int iters = id % 50;
    for (int i = 0; i < iters; i++) {
        yield();
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_1024_varying_mn]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (completed.load() == MAX_FIBERS) {
        printf("  PASS: all %d fibers completed with varying lifetimes\n", MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d, got %d\n", MAX_FIBERS, completed.load());
        return 1;
    }

    return 0;
}
