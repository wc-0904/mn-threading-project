#include <stdio.h>
#include <atomic>
#include "fiber.h"

static std::atomic<int> completed{0};

void child_work(void *arg) {
    completed.fetch_add(1);
}

void parent_work(void *arg) {
    counter_t *c = create_counter(10);
    for (int i = 0; i < 10; i++)
        spawn_with_counter(child_work, nullptr, c);
    wait_for_counter(c, 0);
    
    // if we get here all 10 children finished
    if (completed.load() == 10)
        printf("  PASS: parent resumed after all 10 children completed\n");
    else
        printf("  FAIL: only %d children completed\n", completed.load());
    
    free_counter(c);
}

int main() {
    printf("[test_wait_for_counter]\n");
    scheduler_init();
    spawn(parent_work, nullptr);
    scheduler_run();
    return completed.load() == 10 ? 0 : 1;
}