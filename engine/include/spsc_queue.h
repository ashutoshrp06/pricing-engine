// Lifted verbatim from https://github.com/ashutoshrp06/lob-simulator-optimized
// Cache-line aligned SPSC queue, acquire/release ordering, 6.4ns enqueue.

#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include "order.h"
#include <optional>
#include <array>
#include <atomic>

template <size_t Capacity>
struct SPSCQueue {
    alignas(64) std::atomic<size_t> head = 0;
    alignas(64) std::atomic<size_t> tail= 0;
    std::array<Order, Capacity> array;

    bool push(Order &order) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_acquire);
        if ((h + 1) % Capacity == t) {
            return false;
        }
        array[h] = order;
        head.store((h + 1) % Capacity, std::memory_order_release);
        return true;
    }

    std::optional<Order> pop() {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_relaxed);
        if (h==t) {
            return std::nullopt;
        }
        Order order = array[t];
        tail.store((t + 1) % Capacity, std::memory_order_release);
        return order;
    }
};

#endif
