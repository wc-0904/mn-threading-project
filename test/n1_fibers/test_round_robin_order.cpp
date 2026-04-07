// Verify that fibers execute in strict round-robin order
// Records the order of execution and checks it
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 4
#define ROUNDS 3
#define TOTAL_EVENTS (NUM_FIBERS * ROUNDS)

static int event_log[TOTAL_EVENTS];
static int event_idx = 0;

void my_work(void *arg) {
    int id = *(int*)arg;
    for (int i = 0; i < ROUNDS; i++) {
        event_log[event_idx++] = id;
        yield();
    }
}

int main() {
    printf("[test_round_robin_order]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    // expect: 0,1,2,3, 0,1,2,3, 0,1,2,3
    int pass = 1;
    for (int i = 0; i < TOTAL_EVENTS; i++) {
        int expected = i % NUM_FIBERS;
        if (event_log[i] != expected) {
            printf("  FAIL: event %d was fiber %d, expected %d\n",
                i, event_log[i], expected);
            pass = 0;
        }
    }

    if (pass) {
        printf("  PASS: round-robin order correct across %d rounds\n", ROUNDS);
    }

    return pass ? 0 : 1;
}
