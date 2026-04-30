#include "liquidity_taker.h"
#include <chrono>

using Clock = std::chrono::steady_clock;

// static int64_t now_ns() {
//     return Clock::now().time_since_epoch().count();
// }

LiquidityTaker::LiquidityTaker(const Config& cfg,
                               SPSCQueue<LtOrder, QUEUE_CAPACITY>* queue,
                               std::atomic<bool>* running)
    : cfg_(cfg), queue_(queue), running_(running)
{}

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

    // Poisson arrivals = exponential inter-arrival times
    // arrival_dist gives inter-arrival time in seconds; convert to ns
    auto next_ns = Clock::now().time_since_epoch().count();

    while (*running_) {
        int64_t t = Clock::now().time_since_epoch().count();
        if (t < next_ns) continue;

        LtOrder o;
        o.side                 = side_dist(rng) ? Side::Buy : Side::Sell;
        o.size                 = static_cast<Quantity>(size_dist(rng));
        o.production_timestamp = t;
        queue_->push(o);

        // Schedule next arrival: exponential inter-arrival in seconds -> nanoseconds
        double inter_arrival_s  = arrival_dist(rng);
        next_ns += static_cast<int64_t>(inter_arrival_s * 1e9);
    }
}