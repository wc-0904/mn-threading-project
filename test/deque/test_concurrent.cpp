#include <ws_deque.h>
#include <thread>
#include <atomic>
#include <cassert>
#include <iostream>
#include <vector>

void noop(void*) {}

void test_no_lost_fibers() {
    constexpr int N_FIBERS = 1000;
    constexpr int N_STEALERS = 3;

    deque dq;
    std::vector<fiber_t> fibers(N_FIBERS);
    // track execution count per fiber by index
    std::vector<std::atomic<int>> exec_count(N_FIBERS);
    for (auto& c : exec_count) c.store(0);

    // tag each fiber with its index via args
    for (int i = 0; i < N_FIBERS; i++) {
        fibers[i].func = noop;
        fibers[i].args = (void*)(intptr_t)i;
        fibers[i].state = RUNNABLE;
    }

    std::atomic<int> executed{0};
    std::atomic<bool> done{false};
    std::vector<std::thread> stealers;

    for (int i = 0; i < N_STEALERS; i++) {
        stealers.emplace_back([&]() {
            while (true) {
                fiber_t* f = dq.steal();
                if (f != nullptr && f != reinterpret_cast<fiber_t*>(-1)) {
                    int idx = (intptr_t)f->args;
                    int prev = exec_count[idx].fetch_add(1);
                    assert(prev == 0 && "fiber stolen more than once!");
                    executed.fetch_add(1);
                }
                if (done.load() && dq.steal() == nullptr) break;
            }
        });
    }

    for (int i = 0; i < N_FIBERS; i++) {
        dq.pushBottom(&fibers[i]);
    }

    fiber_t* f;
    while ((f = dq.popBottom()) != nullptr && f != reinterpret_cast<fiber_t*>(-1)) {
        int idx = (intptr_t)f->args;
        int prev = exec_count[idx].fetch_add(1);
        assert(prev == 0 && "fiber popped more than once!");
        executed.fetch_add(1);
    }
    done = true;

    for (auto& t : stealers) t.join();

    assert(executed.load() == N_FIBERS);
    std::cout << "PASS: test_no_lost_fibers\n";
}

int main() {
    test_no_lost_fibers();
    std::cout << "All concurrent deque tests passed.\n";
    return 0;
}