#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

void test_print(void);
void c1_func(void);
void c2_func(void);

Context c1 = {0};
Context c2 = {0};
Context main_ctx = {0};
int i = 0;

int main() {

    // declare stack
    char c1_data[4096];

    // set ptr to the end of stack since stacks grow downwards
    char *sp1 = (char *)(c1_data + sizeof(c1_data));

    // align stack ptr
    sp1 = (char *)((uintptr_t)c1_data & (-16L));

    // create space for redzone
    sp1 -= 128;

    c1.rip = (void *)&c1_func;
    c1.rsp = (void *)sp1;
    
    // set c2 info
    char c2_data[4096];
    char *sp2 = (char *)(c2_data + sizeof(c2_data));
    sp2 = (char *)((uintptr_t)c2_data & (-16L));
    sp2 -= 128;

    c2.rip = (void *)&c2_func;
    c2.rsp = (void *)sp2;

    swap_context(&main_ctx, &c1);

    printf("back to main (%d)...\n", i);
    return 0;
}

void c1_func(void) {
    printf("hello from c1 (%d)...\n", i);
    
    i++;
    if (i < 8) set_context(&c2);
    else swap_context(&c1, &main_ctx);
}

void c2_func(void) {
    printf("hello from c2 (%d)...\n", i);
    
    i++;
    if (i < 8) set_context(&c1);
    else swap_context(&c2, &main_ctx);
}

void test_print(void) {
    printf("hello vro\n");
}