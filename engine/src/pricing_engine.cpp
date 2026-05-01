#include "pricing_engine.h"
#include <chrono>
#include <cmath>
#include <cstdio>

static int64_t now_ns() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

PricingEngine::PricingEngine(const Config& cfg,
                             SPSCQueue<LpQuote,    QUEUE_CAPACITY>* lp_q,
                             SPSCQueue<SignalUpdate,QUEUE_CAPACITY>* sg_q,
                             SPSCQueue<LtOrder,    QUEUE_CAPACITY>* lt_q)
    : cfg_(cfg), lp_q_(lp_q), sg_q_(sg_q), lt_q_(lt_q),
      signal_(0.0),
      rng_(cfg.seed ^ 0xDEADBEEF) // distinct seed from producers
{
    pe_delay_buf_.set_delay_ns(cfg_.pe_to_book_latency_us * 1000);
}

void PricingEngine::run(std::atomic<bool>& running) {
    while (running.load(std::memory_order_relaxed)) {
        pe_delay_buf_.drain([&](const PeQuoteUpdate& u) {
            book_.update_pe_quote(u.bid, u.ask);
        });
        if (auto e = lp_q_->pop())  handle_lp_quote(*e);
        if (auto e = sg_q_->pop())  handle_signal(*e);
        if (auto e = lt_q_->pop())  handle_lt_order(*e);
    }
}

void PricingEngine::handle_lp_quote(const LpQuote& q) {
    book_.update_lp_quote(q.lp_id, q.bid, q.ask);
    reprice();
}

void PricingEngine::handle_signal(const SignalUpdate& s) {
    signal_ = s.value;
    reprice();
}

void PricingEngine::handle_lt_order(const LtOrder& o) {
    int64_t fill_price = 0;
    bool    pe_wins    = false;

    if (o.side == Side::Buy) {
        int tied = 0;
        if (book_.pe_at_best_ask(tied)) {
            std::uniform_int_distribution<int> dist(0, tied);
            pe_wins    = (dist(rng_) == 0);
            fill_price = book_.best_ask();
        }
    } else {
        int tied = 0;
        if (book_.pe_at_best_bid(tied)) {
            std::uniform_int_distribution<int> dist(0, tied);
            pe_wins    = (dist(rng_) == 0);
            fill_price = book_.best_bid();
        }
    }

    if (pe_wins) {
        ++lt_to_pe_count_;
        inventory_.on_fill(o.side, fill_price, o.size, book_.mid(), now_ns());
    } else {
        ++lt_to_lp_count_;
    }

    reprice();
}

void PricingEngine::reprice() {
    int64_t mid = book_.mid();
    if (mid == 0) return; // book not yet populated

    // quote_mid skewed by signal and inventory.
    int64_t quote_mid = mid
        + static_cast<int64_t>(std::round(cfg_.alpha * signal_))
        - static_cast<int64_t>(std::round(cfg_.beta  * static_cast<double>(inventory_.position())));

    // half_spread widens when signal is weak (near 0), tightens when strong (near +-1).
    int64_t half_spread = static_cast<int64_t>(
        std::round(cfg_.base_spread * (1.0 + (1.0 - std::abs(signal_)))));

    pe_bid_ = quote_mid - half_spread;
    pe_ask_ = quote_mid + half_spread;
    pe_delay_buf_.push({pe_bid_, pe_ask_});

    // Hedge if inventory exceeds threshold.
    int64_t pos = inventory_.position();
    if (pos >= cfg_.hedge_threshold) {
        // Long inventory -> sell to best bid LP.
        int64_t hedge_price = book_.best_bid();
        if (hedge_price > 0) {
            int64_t hedge_qty = pos - cfg_.hedge_threshold / 2;
            inventory_.on_hedge(Side::Sell, hedge_price, hedge_qty, now_ns());
            // Requote immediately after hedge changes position.
            quote_mid = mid
                + static_cast<int64_t>(std::round(cfg_.alpha * signal_))
                - static_cast<int64_t>(std::round(cfg_.beta * static_cast<double>(inventory_.position())));
            pe_delay_buf_.push({quote_mid - half_spread, quote_mid + half_spread});
        }
    } else if (pos <= -cfg_.hedge_threshold) {
        // Short inventory -> buy from best ask LP.
        int64_t hedge_price = book_.best_ask();
        if (hedge_price > 0) {
            int64_t hedge_qty = (-pos) - cfg_.hedge_threshold / 2;
            inventory_.on_hedge(Side::Buy, hedge_price, hedge_qty, now_ns());
            // Requote again
            quote_mid = mid
                + static_cast<int64_t>(std::round(cfg_.alpha * signal_))
                - static_cast<int64_t>(std::round(cfg_.beta * static_cast<double>(inventory_.position())));
            pe_delay_buf_.push({quote_mid - half_spread, quote_mid + half_spread});
        }
    }

    inventory_.update_unrealised(mid);
    
    write_snapshot();
}

void PricingEngine::write_snapshot() {
    InventorySnapshot inv = inventory_.snapshot();
    snapshot_.seq.fetch_add(1, std::memory_order_release);
    snapshot_.data.timestamp_ns        = now_ns();
    snapshot_.data.position            = inv.position;
    snapshot_.data.realised_pnl        = inv.realised_pnl;
    snapshot_.data.unrealised_pnl      = inv.unrealised_pnl;
    snapshot_.data.fill_count          = inv.fill_count;
    snapshot_.data.lt_to_pe_count      = lt_to_pe_count_;
    snapshot_.data.lt_to_lp_count      = lt_to_lp_count_;
    snapshot_.data.fill_rate_per_sec   = inv.fill_rate_per_sec;
    snapshot_.data.spread_capture_mean = inv.spread_capture_mean;
    snapshot_.data.mid_price           = book_.mid();
    snapshot_.data.pe_bid              = pe_bid_;
    snapshot_.data.pe_ask              = pe_ask_;
    snapshot_.data.best_bid            = book_.best_bid();
    snapshot_.data.best_ask            = book_.best_ask();
    snapshot_.seq.fetch_add(1, std::memory_order_release);
}