#include "inventory.h"

Inventory::Inventory()
    : position_(0), open_cost_(0), open_qty_(0),
      realised_pnl_(0), unrealised_pnl_(0),
      fill_count_(0), hedge_count_(0),
      spread_capture_sum_(0), mid_at_last_fill_(0),
      fill_ts_head_(0), fill_ts_count_(0)
{
    fill_ts_.fill(0);
}

void Inventory::on_fill(Side side, int64_t price, int64_t qty,
                        int64_t mid_at_fill, int64_t now_ns)
{
    // PE is the maker, so LT buys hit PE's ask (PE sells) and LT sells hit PE's bid (PE buys).
    // So side here is the LT's side. PE takes the opposite.
    int64_t signed_qty = (side == Side::Buy) ? -qty : qty; // PE's position delta.

    // Spread capture -> here the PE earns ask-mid on LT buys, bid-mid on LT sells.
    // LT Buy  -> PE sells at ask -> spread_earned = price - mid (positive if ask > mid)
    // LT Sell -> PE buys  at bid -> spread_earned = mid - price (positive if mid > bid)
    int64_t spread_earned = (side == Side::Buy) ? (price - mid_at_fill)
                                                 : (mid_at_fill - price);
    spread_capture_sum_ += spread_earned;

    // PnL accounting.
    if (open_qty_ == 0) {
        // No open position, so open a new one.
        open_cost_ = price * signed_qty;
        open_qty_  = signed_qty;
    } else if ((open_qty_ > 0) == (signed_qty > 0)) {
        // Adding to existing position in the same direction.
        open_cost_ += price * signed_qty;
        open_qty_  += signed_qty;
    } else {
        // Reducing or closing or flipping.
        int64_t abs_open   = open_qty_  < 0 ? -open_qty_  : open_qty_;
        int64_t abs_new    = signed_qty < 0 ? -signed_qty : signed_qty;
        int64_t close_qty  = abs_new < abs_open ? abs_new : abs_open;

        int64_t avg_open   = open_cost_ / open_qty_;
        int64_t pnl_per_unit = (open_qty_ > 0)
            ? (price - avg_open)
            : (avg_open - price);
        realised_pnl_ += pnl_per_unit * close_qty;

        int64_t remaining = signed_qty + open_qty_; // net after close
        if (remaining == 0) {
            open_cost_ = 0;
            open_qty_  = 0;
        } else {
            // Flip or partial, with new open at current fill price for the excess.
            open_qty_  = remaining;
            open_cost_ = price * remaining;
        }
    }

    position_        += signed_qty;
    int64_t abs_pos = position_ < 0 ? -position_ : position_;
    if (abs_pos > position_peak_abs_) position_peak_abs_ = abs_pos;
    mid_at_last_fill_ = mid_at_fill;
    ++fill_count_;
    record_fill_ts(now_ns);
}

void Inventory::on_hedge(Side side, int64_t price, int64_t qty, int64_t now_ns)
{
    // Hedge is PE actively trading to reduce position.
    // Treated like a fill but no spread capture (we cross the spread to hedge).
    int64_t signed_qty = (side == Side::Buy) ? qty : -qty; // hedge reduces position

    // Same PnL logic as on_fill for the close portion.
    if (open_qty_ != 0) {
        int64_t abs_open  = open_qty_  < 0 ? -open_qty_  : open_qty_;
        int64_t abs_hedge = signed_qty < 0 ? -signed_qty : signed_qty;
        int64_t close_qty = abs_hedge < abs_open ? abs_hedge : abs_open;

        int64_t avg_open     = open_cost_ / open_qty_;
        int64_t pnl_per_unit = (open_qty_ > 0)
            ? (price - avg_open)
            : (avg_open - price);
        realised_pnl_ += pnl_per_unit * close_qty;

        int64_t remaining = open_qty_ + signed_qty;
        if (remaining == 0) {
            open_cost_ = 0;
            open_qty_  = 0;
        } else {
            open_qty_  = remaining;
            open_cost_ = price * remaining;
        }
    }

    position_ += signed_qty;
    int64_t abs_pos = position_ < 0 ? -position_ : position_;
    if (abs_pos > position_peak_abs_) position_peak_abs_ = abs_pos;
    ++hedge_count_;
    (void)now_ns; // reserved for future per-hedge timestamp logging
}

void Inventory::update_unrealised(int64_t current_mid) {
    if (open_qty_ == 0) {
        unrealised_pnl_ = 0;
        return;
    }
    int64_t avg_open = open_cost_ / open_qty_;
    unrealised_pnl_  = (current_mid - avg_open) * position_;
}

InventorySnapshot Inventory::snapshot() const {
    InventorySnapshot s{};
    s.position           = position_;
    s.realised_pnl       = realised_pnl_;
    s.unrealised_pnl     = unrealised_pnl_;
    s.fill_count         = fill_count_;
    s.hedge_count        = hedge_count_;
    s.fill_rate_per_sec  = compute_fill_rate(
        fill_ts_count_ > 0 ? fill_ts_[fill_ts_head_ == 0
            ? FILL_LOG_CAPACITY - 1 : fill_ts_head_ - 1] : 0);
    s.spread_capture_mean = fill_count_ > 0
        ? static_cast<double>(spread_capture_sum_) / fill_count_
        : 0.0;
    s.mid_at_last_fill   = mid_at_last_fill_;
    s.position_peak_abs = position_peak_abs_;
    return s;
}

void Inventory::record_fill_ts(int64_t now_ns) {
    fill_ts_[fill_ts_head_] = now_ns;
    fill_ts_head_ = (fill_ts_head_ + 1) % FILL_LOG_CAPACITY;
    if (fill_ts_count_ < FILL_LOG_CAPACITY) {
        ++fill_ts_count_;
    }
}

double Inventory::compute_fill_rate(int64_t now_ns) const {
    if (fill_ts_count_ == 0 || now_ns == 0) {
        return 0.0;
    }
    constexpr int64_t ONE_SEC_NS = 1'000'000'000LL;
    int count = 0;
    // Walk backwards through circular buffer.
    for (int i = 0; i < fill_ts_count_; ++i) {
        int idx = (fill_ts_head_ - 1 - i + FILL_LOG_CAPACITY) % FILL_LOG_CAPACITY;
        if (now_ns - fill_ts_[idx] <= ONE_SEC_NS) {
            ++count;
        } else break; // timestamps are monotone, stopping early
    }
    return static_cast<double>(count);
}