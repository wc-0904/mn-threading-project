#include <ws_deque.h>
#include <cassert>
#include <iostream>

// dummy work function
void noop(void*) {}

fiber_t make_fiber(void* tag) {
    fiber_t f{};
    f.func = noop;
    f.args = tag;
    f.state = RUNNABLE;
    return f;
}

void test_push_pop_basic() {
    deque dq;
    fiber_t f1 = make_fiber((void*)1);
    fiber_t f2 = make_fiber((void*)2);
    fiber_t f3 = make_fiber((void*)3);

    dq.pushBottom(&f1);
    dq.pushBottom(&f2);
    dq.pushBottom(&f3);

    assert(dq.popBottom() == &f3);
    assert(dq.popBottom() == &f2);
    assert(dq.popBottom() == &f1);
    assert(dq.popBottom() == nullptr);
    std::cout << "PASS: test_push_pop_basic\n";
}

void test_steal_basic() {
    deque dq;
    fiber_t f1 = make_fiber((void*)1);
    fiber_t f2 = make_fiber((void*)2);

    dq.pushBottom(&f1);
    dq.pushBottom(&f2);

    assert(dq.steal() == &f1);  // stealers take from top (FIFO)
    assert(dq.steal() == &f2);
    fiber_t* res = dq.steal();
    assert(res == nullptr || res == reinterpret_cast<fiber_t*>(-1));
    std::cout << "PASS: test_steal_basic\n";
}

void test_grow() {
    deque dq(2);  // log_size=2, capacity=4, will need to grow
    const int N = 10;
    fiber_t fibers[N];
    for (int i = 0; i < N; i++) {
        fibers[i] = make_fiber((void*)(intptr_t)i);
        dq.pushBottom(&fibers[i]);
    }
    for (int i = N - 1; i >= 0; i--) {
        assert(dq.popBottom() == &fibers[i]);
    }
    std::cout << "PASS: test_grow\n";
}

int main() {
    test_push_pop_basic();
    test_steal_basic();
    test_grow();
    std::cout << "All basic deque tests passed.\n";
    return 0;
}