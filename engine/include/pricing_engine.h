#pragma once
#include "consolidated_book.h"
#include "inventory.h"
#include "spsc_queue.h"
#include "events.h"
#include "config.h"
#include "latency_buffer.h"
#include <atomic>
#include <random>
#include <cstdint>
struct MetricsSnapshot {
    int64_t timestamp_ns;
    int64_t position;
    int64_t realised_pnl;
    int64_t unrealised_pnl;
    int64_t fill_count;
    int64_t lt_to_pe_count;
    int64_t lt_to_lp_count;
    double  fill_rate_per_sec;
    double  spread_capture_mean;
    int64_t mid_price;
    int64_t pe_bid;
    int64_t pe_ask;
    int64_t best_bid;
    int64_t best_ask;
};
struct SequencedSnapshot {
    std::atomic<uint64_t> seq{0};   // odd = write in progress
    MetricsSnapshot       data{};
};
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

    const SequencedSnapshot& snapshot() const { return snapshot_; }

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

    SequencedSnapshot snapshot_;
    
    int64_t           pe_bid_ = 0;
    int64_t           pe_ask_ = 0;
    void              write_snapshot();

    LatencyBuffer<PeQuoteUpdate, 256> pe_delay_buf_;
};