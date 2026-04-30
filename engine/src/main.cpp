#include "config.h"
#include "events.h"
#include "spsc_queue.h"
#include "lp_simulator.h"
#include "signal_generator.h"
#include "liquidity_taker.h"

#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

static std::atomic<bool> running{true};

static void signal_handler(int) {
    running = false;
}

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Three queues, one per producer thread
    SPSCQueue<LpQuote,     QUEUE_CAPACITY> lp_queue;
    SPSCQueue<SignalUpdate, QUEUE_CAPACITY> sg_queue;
    SPSCQueue<LtOrder,     QUEUE_CAPACITY> lt_queue;

    LpSimulator     lp_sim(cfg, &lp_queue, &running);
    SignalGenerator sig_gen(cfg, &sg_queue, &running);
    LiquidityTaker  lt(cfg,     &lt_queue, &running);

    // Stub consumer: drain all three queues, count events per second
    uint64_t lp_count = 0, sg_count = 0, lt_count = 0;
    auto last_print = std::chrono::steady_clock::now();

    lp_sim.start();
    sig_gen.start();
    lt.start();

    // Optional duration-based shutdown
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(cfg.duration_s > 0 ? cfg.duration_s : 999999);

    while (running) {
        if (auto q = lp_queue.pop()) { ++lp_count; (void)q; }
        if (auto q = sg_queue.pop()) { ++sg_count; (void)q; }
        if (auto q = lt_queue.pop()) { ++lt_count; (void)q; }

        auto now = std::chrono::steady_clock::now();

        if (now - last_print >= std::chrono::seconds(1)) {
            std::cout << "LP: " << lp_count
                      << "  SG: " << sg_count
                      << "  LT: " << lt_count << "\n";
            lp_count = sg_count = lt_count = 0;
            last_print = now;
        }

        if (cfg.duration_s > 0 && now >= deadline) running = false;
    }

    lp_sim.join();
    sig_gen.join();
    lt.join();

    std::cout << "pricing-engine stopped cleanly\n";
    return 0;
}