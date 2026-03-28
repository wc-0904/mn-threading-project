#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../include/context_swap.h"

void test_print(void);

int main() {
    
    // keep it volatile to prevent caching x
    volatile int x = 0;

    // get current context
    Context c;
    get_context(&c);

    test_print();

    if (x == 0) {
        x++;
        set_context(&c);
    }

    return 0;
}

void test_print(void) {
    printf("hello vro\n");
}