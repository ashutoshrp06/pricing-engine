#pragma once
#include "types.h"
#include <cstdint>
#include <array>

static constexpr int FILL_LOG_CAPACITY = 512;

struct InventorySnapshot {
    int64_t position;
    int64_t realised_pnl;
    int64_t unrealised_pnl;
    int64_t fill_count;
    int64_t hedge_count;
    double  fill_rate_per_sec;
    double  spread_capture_mean; // integer pip-units mean
    int64_t mid_at_last_fill;
    int64_t position_peak_abs;
};

class Inventory {
public:
    Inventory();

    // This will be called by PE on every simulated fill against PE's quote.
    // mid_at_fill is the consolidated mid at the moment of the fill (for spread capture).
    void on_fill(Side side, int64_t price, int64_t qty, int64_t mid_at_fill, int64_t now_ns);

    // Called by PE when a hedge order is emitted.
    void on_hedge(Side side, int64_t price, int64_t qty, int64_t now_ns);

    // Recomputes unrealised PnL from current mid. Call before snapshot.
    void update_unrealised(int64_t current_mid);

    InventorySnapshot snapshot() const;

    int64_t position()    const { return position_; }
    int64_t realised_pnl() const { return realised_pnl_; }

private:
    void record_fill_ts(int64_t now_ns);
    double compute_fill_rate(int64_t now_ns) const;

    int64_t position_;
    int64_t open_cost_;      // sum(price * signed_qty) for open position
    int64_t open_qty_;       // same sign as position_
    int64_t realised_pnl_;
    int64_t unrealised_pnl_;
    int64_t position_peak_abs_ = 0;

    int64_t fill_count_;
    int64_t hedge_count_;

    // Spread capture, i. e. running sum in pip-units, signed by maker side.
    int64_t spread_capture_sum_;

    int64_t mid_at_last_fill_;

    // Circular buffer of fill timestamps for rolling fill-rate.
    std::array<int64_t, FILL_LOG_CAPACITY> fill_ts_;
    int fill_ts_head_;  // next write index
    int fill_ts_count_; // entries valid, capped at FILL_LOG_CAPACITY
};