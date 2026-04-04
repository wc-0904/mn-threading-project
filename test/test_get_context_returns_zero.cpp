// Verify get_context returns 0 (xorl %eax, %eax)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"

int main() {
    printf("[test_get_context_returns_zero]\n");

    Context c;
    int (*get_ctx_int)(Context*) = (int(*)(Context*))get_context;
    int ret = get_ctx_int(&c);

    if (ret == 0) {
        printf("  PASS: get_context returns 0\n");
    } else {
        printf("  FAIL: got %d instead of 0\n", ret);
        return 1;
    }

    return 0;
}
