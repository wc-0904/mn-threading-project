#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_PARENTS         8
#define CHILDREN_PER_PARENT 16

static std::atomic<int> total_completed{0};
static std::atomic<int> parents_verified{0};
static int ids[NUM_PARENTS];

void child_work(void *arg) {
    total_completed.fetch_add(1);
}

void parent_work(void *arg) {
    int id = *(int*)arg;
    counter_t *c = create_counter(CHILDREN_PER_PARENT);
    for (int i = 0; i < CHILDREN_PER_PARENT; i++)
        spawn_with_counter(child_work, &ids[id], c);
    wait_for_counter(c, 0);
    parents_verified.fetch_add(1);
    free_counter(c);
}

int main() {
    printf("[test_wait_for_counter_stress]\n");
    scheduler_init();
    for (int i = 0; i < NUM_PARENTS; i++) {
        ids[i] = i;
        spawn(parent_work, &ids[i]);
    }
    scheduler_run();

    int expected = NUM_PARENTS * CHILDREN_PER_PARENT;
    if (total_completed.load() == expected && parents_verified.load() == NUM_PARENTS) {
        printf("  PASS: %d parents each verified %d children = %d total\n",
               NUM_PARENTS, CHILDREN_PER_PARENT, expected);
        return 0;
    } else {
        printf("  FAIL: completed=%d expected=%d parents_verified=%d\n",
               total_completed.load(), expected, parents_verified.load());
        return 1;
    }
}