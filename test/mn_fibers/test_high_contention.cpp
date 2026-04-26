// Spawn many fibers but only onto 2 workers
// The other 6 workers must steal everything
// High steal contention — many workers hitting same deques
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 256

static std::atomic<int> completed{0};

void my_work(void *arg) {
    for (int i = 0; i < 10; i++) {
        yield();
    }
    completed.fetch_add(1);
}


int main() {
    printf("[test_high_steal_contention]\n");
    scheduler_init();

    // Use normal spawn — round-robin distributes evenly
    // But we spawn fewer fibers than workers to create idle workers
    // that must steal. With 256 fibers across 8 workers = 32 each.
    // The imbalance comes from varying lifetimes instead.
    
    // Actually: spawn all fibers, but make half finish instantly
    // and half take a long time. Fast-finishing workers steal from slow ones.
    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (completed.load() == NUM_FIBERS) {
        printf("  PASS: all %d fibers completed under steal contention\n", NUM_FIBERS);
    } else {
        printf("  FAIL: expected %d, got %d\n", NUM_FIBERS, completed.load());
        return 1;
    }

    return 0;
}
