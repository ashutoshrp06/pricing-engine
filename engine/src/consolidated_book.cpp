#include "consolidated_book.h"
#include <limits>

ConsolidatedBook::ConsolidatedBook()
    : best_bid_(0), best_ask_(0),
      best_bid_lp_id_(-1), best_ask_lp_id_(-1)
{
    bids_.fill(0);
    asks_.fill(0);
}

void ConsolidatedBook::update_lp_quote(int lp_id, int64_t bid, int64_t ask) {
    bids_[lp_id] = bid;
    asks_[lp_id] = ask;
    rescan();
}

void ConsolidatedBook::update_pe_quote(int64_t bid, int64_t ask) {
    bids_[0] = bid;
    asks_[0] = ask;
    rescan();
}

int64_t ConsolidatedBook::mid() const {
    if (best_bid_ == 0 || best_ask_ == 0) return 0;
    return (best_bid_ + best_ask_) / 2;
}

int64_t ConsolidatedBook::lp_mid() const {
    int64_t lp_bb = 0;
    int64_t lp_ba = std::numeric_limits<int64_t>::max();
    for (int i = 1; i < NUM_PARTICIPANTS; ++i) {  // skip slot 0 (PE)
        if (bids_[i] > lp_bb) lp_bb = bids_[i];
        if (asks_[i] > 0 && asks_[i] < lp_ba) lp_ba = asks_[i];
    }
    if (lp_bb == 0 || lp_ba == std::numeric_limits<int64_t>::max()) return 0;
    return (lp_bb + lp_ba) / 2;
}


int64_t ConsolidatedBook::spread() const {
    if (best_bid_ == 0 || best_ask_ == 0) return 0;
    return best_ask_ - best_bid_;
}

void ConsolidatedBook::rescan() {
    int64_t bb = 0;    // best bid temp
    int64_t ba = std::numeric_limits<int64_t>::max();    // best ask temp
    int     bb_id = -1;
    int     ba_id = -1;

    for (int i = 0; i < NUM_PARTICIPANTS; ++i) {
        if (bids_[i] > bb) {
            bb    = bids_[i];
            bb_id = i;
        }
        // 0 means not quoting, so skip on ask side.
        if (asks_[i] > 0 && asks_[i] < ba) {
            ba    = asks_[i];
            ba_id = i;
        }
    }

    best_bid_       = bb;
    best_ask_       = (ba == std::numeric_limits<int64_t>::max()) ? 0 : ba;
    best_bid_lp_id_ = bb_id;
    best_ask_lp_id_ = ba_id;
}

bool ConsolidatedBook::pe_at_best_bid(int& tied_lp_count) const {
    if (best_bid_ == 0 || bids_[0] != best_bid_) {
        return false;
    }
    tied_lp_count = 0;
    for (int i = 1; i < NUM_PARTICIPANTS; ++i) {
        if (bids_[i] == best_bid_) { 
            ++tied_lp_count;
        }
    }
    return true;
}

bool ConsolidatedBook::pe_at_best_ask(int& tied_lp_count) const {
    if (best_ask_ == 0 || asks_[0] != best_ask_) {
        return false;
    }
    tied_lp_count = 0;
    for (int i = 1; i < NUM_PARTICIPANTS; ++i) {
        if (asks_[i] == best_ask_) {
            ++tied_lp_count;
        }
    }
    return true;
}