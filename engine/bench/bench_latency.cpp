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

int main() {
    Config cfg{};
    cfg.num_lps               = 12;
    cfg.lp_quote_hz            = 500;
    cfg.signal_hz        = 100;
    cfg.lt_arrival_hz         = 50;
    cfg.seed                  = 42;
    cfg.lp_to_pe_latency_us   = 0;
    cfg.lt_to_pe_latency_us   = 0;
    cfg.pe_to_book_latency_us = 0;
    cfg.duration_s            = 0;

    SPSCQueue<LpQuote,      QUEUE_CAPACITY> lp_q;
    SPSCQueue<SignalUpdate,  QUEUE_CAPACITY> sg_q;
    SPSCQueue<LtOrder,       QUEUE_CAPACITY> lt_q;

    std::atomic<bool> running{true};

    PricingEngine  pe(cfg, &lp_q, &sg_q, &lt_q);
    LpSimulator     lp(cfg, &lp_q, &running);
    SignalGenerator sg(cfg, &sg_q, &running);
    LiquidityTaker  lt(cfg, &lt_q, &running);


    std::thread t_lp([&]{ lp.run(); });
    std::thread t_sg([&]{ sg.run(); });
    std::thread t_lt([&]{ lt.run(); });
    std::thread t_pe([&]{ pe.run(running); });

    std::this_thread::sleep_for(std::chrono::seconds(60));

    running = false;
    t_pe.join(); t_lp.join(); t_sg.join(); t_lt.join();

    // read_snapshot pattern from main.cpp
    MetricsSnapshot snap{};
    uint64_t s1, s2;
    do {
        s1   = pe.snapshot().seq.load(std::memory_order_acquire);
        snap = pe.snapshot().data;
        s2   = pe.snapshot().seq.load(std::memory_order_acquire);
    } while (s1 != s2 || (s1 & 1));

    // lat_count is not exposed directly; sample size approximated from
    // events at default load: (12*500+100+50)*60 = 369000, sampled every
    // N events. Actual sample size = lat_count_ capped at LAT_BUF_SIZE=2048.
    int64_t sample_size = 2048;   // conservative: buf is full after approx. first second

    std::printf("   Latency Benchmark    \n");
    std::printf("Duration    : 60 s  (default load: 12 LP x 500 Hz, SG 100 Hz, LT 50 Hz)\n");
    std::printf("Sample size : ~%lld events\n", (long long)sample_size);
    std::printf("p50         : %lld ns\n",  (long long)snap.latency_p50_ns);
    std::printf("p99         : %lld ns\n",  (long long)snap.latency_p99_ns);
    std::printf("p99.9       : %lld ns\n",  (long long)snap.latency_p99_9_ns);
    std::printf("max         : %lld ns\n",  (long long)snap.latency_max_ns);

    return 0;
}