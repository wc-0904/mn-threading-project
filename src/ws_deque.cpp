#include "ws_deque.h"
#include <iostream>
#include "fiber.h"

deque::deque(int initial_log_size)
    : bottom(0), top(0) {
    active_array.store(new circular_buffer(initial_log_size));
    Empty = nullptr;
    Abort = reinterpret_cast<fiber_t*>(-1);
}

bool deque::cas_top(long old_val, long new_val) {
    return top.compare_exchange_strong(
        old_val, new_val,
        std::memory_order_seq_cst
    );
}

void deque::pushBottom(fiber_t* fiber) {
    long b = bottom.load(std::memory_order_relaxed);
    long t = top.load(std::memory_order_acquire);

    circular_buffer* a = active_array.load(std::memory_order_relaxed);

    long size = b - t;
    if (size >= a->size() - 1) {
        a = a->grow(b, t);
        active_array.store(a, std::memory_order_relaxed);
    }

    a->put(b, fiber);
    bottom.store(b + 1, std::memory_order_release);
}

fiber_t* deque::popBottom() {
    long b = bottom.load(std::memory_order_relaxed) - 1;
    circular_buffer* a = active_array.load(std::memory_order_relaxed);
    bottom.store(b, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    long t = top.load(std::memory_order_relaxed);
    long size = b - t;

    if (size < 0) {
        bottom.store(t, std::memory_order_relaxed);
        return Empty;
    }

    fiber_t* fiber = a->get(b);
    if (size > 0) return fiber;

    if (!cas_top(t, t + 1)) {
        fiber = Empty;
    }

    bottom.store(t + 1, std::memory_order_relaxed);
    return fiber;
}

fiber_t* deque::steal() {
    long t = top.load(std::memory_order_acquire);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    long b = bottom.load(std::memory_order_acquire);
    circular_buffer* a = active_array.load(std::memory_order_acquire);

    long size = b - t;
    if (size <= 0) return Empty;

    fiber_t* fiber = a->get(t);
    if (!cas_top(t, t + 1)) return Abort;

    return fiber;
}