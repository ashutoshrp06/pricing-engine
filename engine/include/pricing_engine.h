#pragma once
#include "consolidated_book.h"
#include "inventory.h"
#include "spsc_queue.h"
#include "events.h"
#include "config.h"
#include <atomic>
#include <random>
#include <cstdint>

class PricingEngine {
public:
    PricingEngine(const Config& cfg,
                  SPSCQueue<LpQuote,     QUEUE_CAPACITY>* lp_q,
                  SPSCQueue<SignalUpdate,QUEUE_CAPACITY>* sg_q,
                  SPSCQueue<LtOrder,     QUEUE_CAPACITY>* lt_q);

    void run(std::atomic<bool>& running);

    const Inventory&         inventory()    const { return inventory_; }
    const ConsolidatedBook&  book()         const { return book_; }
    double                   last_signal()  const { return signal_; }
    
    int64_t lt_to_lp_count() const { return lt_to_lp_count_; }

private:
    void handle_lp_quote(const LpQuote& q);
    void handle_signal(const SignalUpdate& s);
    void handle_lt_order(const LtOrder& o);
    void reprice();

    const Config& cfg_;

    SPSCQueue<LpQuote,      QUEUE_CAPACITY>* lp_q_;
    SPSCQueue<SignalUpdate, QUEUE_CAPACITY>* sg_q_;
    SPSCQueue<LtOrder,      QUEUE_CAPACITY>* lt_q_;

    ConsolidatedBook book_;
    Inventory        inventory_;

    double       signal_;
    std::mt19937 rng_;

    int64_t lt_to_lp_count_ = 0;
    int64_t lt_to_pe_count_ = 0;
};