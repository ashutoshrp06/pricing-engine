#include "config.h"
#include "events.h"
#include "spsc_queue.h"
#include "lp_simulator.h"
#include "signal_generator.h"
#include "liquidity_taker.h"
#include "pricing_engine.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <vector>

int main() {
    Config cfg{};
    cfg.num_lps              = 12;
    cfg.lp_quote_hz           = 1000;
    cfg.signal_hz       = 1000;
    cfg.lt_arrival_hz        = 1000;
    cfg.seed                 = 42;
    cfg.lp_to_pe_latency_us  = 0;
    cfg.lt_to_pe_latency_us  = 0;
    cfg.pe_to_book_latency_us= 0;
    cfg.duration_s           = 0;   // producers run until 'running' goes false

    SPSCQueue<LpQuote,     QUEUE_CAPACITY> lp_q;
    SPSCQueue<SignalUpdate, QUEUE_CAPACITY> sg_q;
    SPSCQueue<LtOrder,     QUEUE_CAPACITY> lt_q;

    std::atomic<bool> running{true};

    PricingEngine pe(cfg, &lp_q, &sg_q, &lt_q);
    LpSimulator     lp(cfg, &lp_q, &running);
    SignalGenerator sg(cfg, &sg_q, &running);
    LiquidityTaker  lt(cfg, &lt_q, &running);

    std::thread t_lp([&]{ lp.run(); });
    std::thread t_sg([&]{ sg.run(); });
    std::thread t_lt([&]{ lt.run(); });
    std::thread t_pe([&]{ pe.run(running); });

    constexpr int DURATION_S  = 60;
    constexpr int SAMPLE_HZ   = 10;   // queue depth sampled 10x/sec
    constexpr int TOTAL_TICKS = DURATION_S * SAMPLE_HZ;

    std::vector<uint64_t> per_sec_drained;
    per_sec_drained.reserve(DURATION_S);

    uint64_t prev        = 0;
    uint64_t max_drained = 0;

    // Sample at 10 Hz, accumulate into 1-second buckets
    uint64_t bucket = 0;
    int      ticks_in_bucket = 0;

    for (int i = 0; i < TOTAL_TICKS; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / SAMPLE_HZ));
        uint64_t cur   = pe.events_drained();
        uint64_t delta = cur - prev;
        prev           = cur;
        bucket        += delta;
        ++ticks_in_bucket;

        if (ticks_in_bucket == SAMPLE_HZ) {
            per_sec_drained.push_back(bucket);
            max_drained      = std::max(max_drained, bucket);
            bucket           = 0;
            ticks_in_bucket  = 0;
        }
    }

    running = false;
    t_pe.join(); t_lp.join(); t_sg.join(); t_lt.join();

    //uint64_t total = 0;
    constexpr size_t SKIP = 2;
    auto begin = per_sec_drained.begin() + std::min(SKIP, per_sec_drained.size());
    std::sort(begin, per_sec_drained.end());
    size_t n = per_sec_drained.end() - begin;
    double sustained = static_cast<double>(*(begin + n / 2));

    // Target: 12*1000 (LP) + 1000 (SG) + 1000 (LT) = 14000 events/sec
    std::printf(".  Throughput Benchmark  \n");
    std::printf("Duration          : %d s\n",          DURATION_S);
    std::printf("Target throughput : 14000 events/s\n");
    std::printf("Sustained (median): %.0f events/s\n", sustained);
    std::printf("Peak (1s window)  : %llu events/s\n", (unsigned long long)max_drained);

    return 0;
}