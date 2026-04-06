// Simple producer-consumer using a shared buffer
// Producer writes values, yields, consumer reads them
// Since N:1 is single-threaded, ordering is deterministic
#include <stdio.h>
#include "fiber.h"

#define NUM_ITEMS 10

static int buffer[NUM_ITEMS];
static int write_idx = 0;
static int read_idx = 0;
static int items_produced = 0;
static int items_consumed = 0;
static int consume_sum = 0;

void producer(void *arg) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        buffer[write_idx++] = i * 10;
        items_produced++;
        printf("  produced: %d\n", i * 10);
        yield();
    }
}

void consumer(void *arg) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        // keep yielding until something is available
        while (read_idx >= write_idx) {
            yield();
        }
        int val = buffer[read_idx++];
        consume_sum += val;
        items_consumed++;
        printf("  consumed: %d\n", val);
        yield();
    }
}

int main() {
    printf("[test_producer_consumer]\n");
    scheduler_init();

    spawn(producer, NULL);
    spawn(consumer, NULL);

    scheduler_run();

    // sum of 0+10+20+...+90 = 450
    int expected_sum = 0;
    for (int i = 0; i < NUM_ITEMS; i++) {
        expected_sum += i * 10;
    }

    if (items_produced == NUM_ITEMS &&
        items_consumed == NUM_ITEMS &&
        consume_sum == expected_sum) {
        printf("  PASS: produced %d, consumed %d, sum = %d\n",
            items_produced, items_consumed, consume_sum);
    } else {
        printf("  FAIL: produced=%d consumed=%d sum=%d (expected %d,%d,%d)\n",
            items_produced, items_consumed, consume_sum,
            NUM_ITEMS, NUM_ITEMS, expected_sum);
        return 1;
    }

    return 0;
}
