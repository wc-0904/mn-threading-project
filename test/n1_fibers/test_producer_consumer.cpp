#include <stdio.h>
#include "fiber.h"

#define NUM_ITEMS 10

static int buffer[NUM_ITEMS];
static int items_produced = 0;
static int items_consumed = 0;
static int consume_sum = 0;

void producer(void *arg) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        buffer[i] = i * 10;
        items_produced++;
    }
}

void consumer(void *arg) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        consume_sum += buffer[i];
        items_consumed++;
    }
}

int main() {
    printf("[test_producer_consumer]\n");
    scheduler_init();

    // spawn consumer first so producer is on top (LIFO),
    // producer runs to completion, then consumer reads full buffer
    spawn(consumer, NULL);
    spawn(producer, NULL);

    scheduler_run();

    int expected_sum = 0;
    for (int i = 0; i < NUM_ITEMS; i++) expected_sum += i * 10;

    if (items_produced == NUM_ITEMS &&
        items_consumed == NUM_ITEMS &&
        consume_sum == expected_sum) {
        printf("  PASS: produced %d, consumed %d, sum = %d\n",
               items_produced, items_consumed, consume_sum);
        return 0;
    } else {
        printf("  FAIL: produced=%d consumed=%d sum=%d (expected %d,%d,%d)\n",
               items_produced, items_consumed, consume_sum,
               NUM_ITEMS, NUM_ITEMS, expected_sum);
        return 1;
    }
}