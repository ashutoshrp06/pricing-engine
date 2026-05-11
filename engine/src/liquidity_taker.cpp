#include "liquidity_taker.h"
#include <chrono>

using Clock = std::chrono::steady_clock;

LiquidityTaker::LiquidityTaker(const Config& cfg,
                               SPSCQueue<LtOrder, QUEUE_CAPACITY>* queue,
                               std::atomic<bool>* running)
    : cfg_(cfg), queue_(queue), running_(running)
{
    delay_buf_.set_delay_ns(cfg_.lt_to_pe_latency_us * 1000);
}

void LiquidityTaker::start() {
    thread_ = std::thread(&LiquidityTaker::run, this);
}

void LiquidityTaker::join() {
    if (thread_.joinable()) thread_.join();
}

void LiquidityTaker::run() {
    std::mt19937                          rng(cfg_.seed ^ 0xDEAD0002ULL);
    std::exponential_distribution<double> arrival_dist(cfg_.lt_arrival_hz);
    std::uniform_int_distribution<int>    side_dist(0, 1);
    std::uniform_int_distribution<int>    size_dist(1, 10);

    auto next_ns = Clock::now().time_since_epoch().count();

    while (*running_) {
        int64_t t = Clock::now().time_since_epoch().count();

        if (t >= next_ns) {
            LtOrder o;
            o.side                 = side_dist(rng) ? Side::Buy : Side::Sell;
            o.size                 = static_cast<Quantity>(size_dist(rng));
            o.production_timestamp = t;
            delay_buf_.push(o);

            double inter_arrival_s = arrival_dist(rng);
            next_ns += static_cast<int64_t>(inter_arrival_s * 1e9);
        }

        delay_buf_.drain([&](const LtOrder& o) {
            queue_->push(o);
        });
    }
}