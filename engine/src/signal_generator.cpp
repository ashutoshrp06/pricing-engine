#include "signal_generator.h"
#include <chrono>

using Clock = std::chrono::steady_clock;

static int64_t now_ns() {
    return Clock::now().time_since_epoch().count();
}

SignalGenerator::SignalGenerator(const Config& cfg,
                                 SPSCQueue<SignalUpdate, QUEUE_CAPACITY>* queue,
                                 std::atomic<bool>* running)
    : cfg_(cfg), queue_(queue), running_(running)
{}

void SignalGenerator::start() {
    thread_ = std::thread(&SignalGenerator::run, this);
}

void SignalGenerator::join() {
    if (thread_.joinable()) thread_.join();
}

void SignalGenerator::run() {
    std::mt19937 rng(cfg_.seed ^ 0xABCD1234ULL);  // distinct seed from LPs
    std::uniform_real_distribution<double> step_dist(-0.05, 0.05);

    const int64_t interval_ns = 1'000'000'000 / cfg_.signal_hz;
    int64_t next_ns = now_ns();
    double value = 0.0;  // signal starts neutral

    while (*running_) {
        int64_t t = now_ns();
        if (t < next_ns) continue;

        value += step_dist(rng);
        // Clamp at [-1, 1]. Reflection would be more symmetric statistically but the difference is invisible at the scales used here.
        if (value >  1.0) value =  1.0;
        if (value < -1.0) value = -1.0;

        SignalUpdate u;
        u.value                = value;
        u.production_timestamp = t;
        queue_->push(u);

        next_ns += interval_ns;
    }
}