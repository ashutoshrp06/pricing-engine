#pragma once
#include "types.h"
#include <cstdint>
#include <array>

// Top-of-book aggregator across all LPs and PE.
// All prices in integer pip-units (mid ~110000).
// Single-threaded, only the PE consumer thread writes and reads.

static constexpr int MAX_LPS = 12;
static constexpr int NUM_PARTICIPANTS = MAX_LPS + 1; // 0=PE, 1..12=LPs

class ConsolidatedBook {
public:
    ConsolidatedBook();

    void update_lp_quote(int lp_id, int64_t bid, int64_t ask);
    void update_pe_quote(int64_t bid, int64_t ask);

    int64_t best_bid()      const { return best_bid_; }
    int64_t best_ask()      const { return best_ask_; }
    int     best_bid_lp_id() const { return best_bid_lp_id_; }
    int     best_ask_lp_id() const { return best_ask_lp_id_; }

    // Returns 0 if either side absent.
    int64_t mid()    const;
    int64_t spread() const;

    // For LT matching
    // also fills out how many LPs are tied with PE at that best.
    bool pe_at_best_bid(int& tied_lp_count) const;
    bool pe_at_best_ask(int& tied_lp_count) const;

private:
    void rescan();

    std::array<int64_t, NUM_PARTICIPANTS> bids_;
    std::array<int64_t, NUM_PARTICIPANTS> asks_;

    int64_t best_bid_;
    int64_t best_ask_;
    int     best_bid_lp_id_;
    int     best_ask_lp_id_;
};