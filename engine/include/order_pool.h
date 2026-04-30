#ifndef ORDER_POOL_H
#define ORDER_POOL_H

#include "order.h"
#include <vector>
#include <stdexcept>
#include <iostream>

class OrderPool {
public:
    explicit OrderPool(size_t capacity) 
        : orders_(capacity), capacity_(capacity) {
        // Build free list - all indices are initially free
        free_list_.reserve(capacity);
        for (size_t i = 0; i < capacity; i++) {
            free_list_.push_back(static_cast<OrderIndex>(i));
        }
    }
    
    // Allocate returns an index, not a pointer
    OrderIndex allocate() {
        if (free_list_.empty()) {
            std::cerr << "Order pool exhausted\n";
            std::abort();
        }
        OrderIndex idx = free_list_.back();
        free_list_.pop_back();
        return idx;
    }
    
    void deallocate(OrderIndex idx) {
        // Reset the order's list pointers
        if (idx == INVALID_INDEX || idx >= capacity_) {
            std::cerr << "BAD deallocate: " << idx << "\n";
            std::abort();
        }
        orders_[idx].next = INVALID_INDEX;
        orders_[idx].prev = INVALID_INDEX;
        free_list_.push_back(idx);
    }
    
    // Access order by index
    Order& get(OrderIndex idx) {
        if (idx == INVALID_INDEX || idx >= capacity_) {
            std::cerr << "INVALID pool access: " << idx << "\n";
            std::abort();
        }
        return orders_[idx];
    }
    
    const Order& get(OrderIndex idx) const {
        return orders_[idx];
    }
    
    size_t available() const {
        return free_list_.size();
    }
    
    size_t capacity() const {
        return capacity_;
    }

private:
    std::vector<Order> orders_;     // Contiguous storage
    std::vector<OrderIndex> free_list_;  // Available indices
    size_t capacity_;
};

#endif // ORDER_POOL_H