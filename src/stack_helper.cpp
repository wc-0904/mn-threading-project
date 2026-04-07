#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

// align a stack and return the aligned stack pointer
void *align_stack(char* raw_stack, size_t size) {
    char *sp = raw_stack + size;
    sp = (char*)((uintptr_t)sp & -16L);
    sp -= 128; // red zone
    return (void*)sp;
}

