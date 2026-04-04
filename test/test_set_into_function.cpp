// set_context into a standalone function with its own stack
// Uses fork so exit() doesn't kill the test
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include "context_swap.h"

static void standalone_func() {
    printf("  entered standalone_func\n");
    _exit(42);
}

int main() {
    printf("[test_set_into_function]\n");

    pid_t pid = fork();
    if (pid == 0) {
        static char stack[8192];
        void *sp = align_stack(stack, sizeof stack);

        Context c = {};
        c.rip = (void*)standalone_func;
        c.rsp = sp;

        set_context(&c);
        _exit(99);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 42) {
        printf("  PASS: jumped into standalone function\n");
    } else {
        printf("  FAIL: wrong exit code or crash\n");
        return 1;
    }

    return 0;
}
