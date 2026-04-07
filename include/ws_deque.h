#include <circular_buffer.h>
#include <atomic>
#include <iostream>

#define K 3

/* source: https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf*/
// todo: figure out how to do this with the shared pool model
// todo comment with function headers + good style 

class deque {
    private: 
        std::atomic<long> bottom{0};
        std::atomic<long> top{0};
        std::atomic<circular_buffer*> active_array;

        fiber_t* Empty = nullptr; 
        fiber_t* Abort = reinterpret_cast<fiber_t*>(-1);

        bool cas_top(long old_val, long new_val) {
            return top.compare_exchange_strong(old_val, new_val, 
                                            std::memory_order_seq_cst);
        }

    public: 
        deque(int initial_log_size = 5) { 
            active_array.store(new circular_buffer(initial_log_size));
        }

        void pushBottom(fiber_t* fiber) {
            long b = bottom.load(std::memory_order_relaxed); // read atomic variable 
            long t = top.load(std::memory_order_acquire);    // before reading pull all changes
            // bottom is modified by owner, top is modified by stealers, so sync t
            
            circular_buffer* a = active_array.load(std::memory_order_relaxed); // read 
            
            long size = b - t;
            if (size >= a->size() - 1) { // if we are out of space, increase array size 
                a = a->grow(b, t);
                active_array.store(a, std::memory_order_relaxed);   // restore existing elements
            }
            
            a->put(b, fiber);
            // ensure fiber is written before bottom is visible to stealers
            bottom.store(b + 1, std::memory_order_release);
        }

        fiber_t* popBottom() {
            long b = bottom.load(std::memory_order_relaxed) - 1;
            circular_buffer* a = active_array.load(std::memory_order_relaxed);
            bottom.store(b, std::memory_order_relaxed);

            // fence prevents top from being read before bottom is stored
            std::atomic_thread_fence(std::memory_order_seq_cst); // for pop + steal
            
            long t = top.load(std::memory_order_relaxed);
            long size = b - t;

            if (size < 0) {
                bottom.store(t, std::memory_order_relaxed);
                return Empty;
            }

            fiber_t* fiber = a->get(b);
            if (size > 0) return fiber;

            // Last element case
            if (!cas_top(t, t + 1)) {
                fiber = Empty;
            }
            bottom.store(t + 1, std::memory_order_relaxed);
            return fiber;
        }

        fiber_t* steal() {
            long t = top.load(std::memory_order_acquire);
            
            // Ensure we see the most recent pushBottom data
            std::atomic_thread_fence(std::memory_order_seq_cst);
            
            // ensure all owner changes propogated before looking at bottom
            long b = bottom.load(std::memory_order_acquire); 
            circular_buffer* a = active_array.load(std::memory_order_acquire);

            long size = b - t;
            if (size <= 0) return Empty;

            fiber_t* fiber = a->get(t);
            if (!cas_top(t, t + 1)) return Abort;

            return fiber;
        }
};