#pragma once

#include <atomic>
#include "circular_buffer.h"
#include "fiber.h"


class deque {
private:
    std::atomic<long> bottom;
    std::atomic<long> top;
    std::atomic<circular_buffer*> active_array;

    fiber_t* Empty;
    fiber_t* Abort;

    bool cas_top(long old_val, long new_val);

public:
    deque(int initial_log_size = 5);

    void pushBottom(fiber_t* fiber);
    fiber_t* popBottom();
    fiber_t* steal();
};