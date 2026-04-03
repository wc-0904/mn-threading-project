// #include <stdio.h>
// #include <stdlib.h>
#include <stdint.h>

struct Context {
  void *rip, *rsp;
  void *rbx, *rbp, *r12, *r13, *r14, *r15;
};

#ifdef __cplusplus
extern "C" {
#endif

void get_context(Context *c);
void set_context(Context *c);
void swap_context(Context *c1, Context *c2);

#ifdef __cplusplus
}
#endif

// extern void context_swap(void);
// extern void get_context(Context c);
// extern void set_context(Context c);