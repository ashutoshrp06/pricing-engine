#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cassert>

// Fixed-capacity constant-delay FIFO ring buffer.
// Items are released only after delay_ns nanoseconds have elapsed since push.
// Because delay is constant, items always exit in insertion order, so no sorting needed.
template<typename T, std::size_t N>
class LatencyBuffer {
public:
    void set_delay_ns(int64_t ns) { delay_ns_ = ns; }
    int64_t delay_ns() const { return delay_ns_; }

    // Returns false if buffer is full, caller should not drop silently in prod.
    // At configured rates and max 10ms delay, this should never trigger.
    bool push(const T& item) {
        if (size_ == N) {
            return false;
        }
        int64_t now = now_ns();
        buf_[tail_] = { now + delay_ns_, item };
        tail_ = (tail_ + 1) % N;
        ++size_;
        return true;
    }

    // Calls cb(item) for every item whose release_time has passed, in FIFO order.
    template<typename F>
    void drain(F&& cb) {
        int64_t now = now_ns();
        while (size_ > 0 && buf_[head_].release_time_ns <= now) {
            cb(buf_[head_].item);
            head_ = (head_ + 1) % N;
            --size_;
        }
    }

    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }

private:
    struct Slot { int64_t release_time_ns; T item; };
    std::array<Slot, N> buf_{};
    std::size_t head_ = 0, tail_ = 0, size_ = 0;
    int64_t delay_ns_ = 0;

    static int64_t now_ns() {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }
};