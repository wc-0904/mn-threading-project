#include <fiber.h>
#include <vector>

class circular_buffer {
    private:
        long log_size;
        fiber_t** segment; // Array of fiber pointers

    public: 
        circular_buffer(long log_size) : log_size(log_size) {
            segment = new fiber_t*[1L << log_size];
        }

        ~circular_buffer() {
            delete[] segment;
        }

        long size() {
            return 1L << log_size;
        }

        fiber_t* get(long i) {
            return segment[i & (size() - 1)];
        }

        void put(long i, fiber_t* fiber) {
            segment[i & (size() - 1)] = fiber;
        }

        circular_buffer* grow(long b, long t) {
            circular_buffer* nw = new circular_buffer(log_size + 1);
            for (long i = t; i < b; i++) {
                nw->put(i, this->get(i));
            }
            return nw;
        }
};