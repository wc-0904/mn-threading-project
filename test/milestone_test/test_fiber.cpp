#include <stdio.h>
#include <time.h>
#include "fiber.h"

#define NUM_FIBERS 1024
#define YIELDS_PER_FIBER 50
#define TOTAL_SWITCHES (NUM_FIBERS * YIELDS_PER_FIBER)

void my_work(void *arg) {
    for (int i = 0; i < YIELDS_PER_FIBER; i++)
        yield();
}

int main() {
    scheduler_init();
    for (int i = 0; i < NUM_FIBERS; i++)
        spawn(my_work, NULL);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    scheduler_run();
    clock_gettime(CLOCK_MONOTONIC, &end);

    long ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("total switches: %d\n", TOTAL_SWITCHES);
    printf("total time:     %ld ns\n", ns);
    printf("per switch:     %.1f ns\n", (double)ns / TOTAL_SWITCHES);
    return 0;
}