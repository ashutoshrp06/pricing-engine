#ifndef LP_SIMULATOR_H
#define LP_SIMULATOR_H

#include "config.h"
#include "events.h"
#include "spsc_queue.h"
#include "latency_buffer.h"
#include <atomic>
#include <random>
#include <vector>
#include <thread>

class LpSimulator {
public:
    // queue is owned externally, passed in by pointer
    // running is shared with main, checked each tick to know when to stop
    LpSimulator(const Config& cfg,
                SPSCQueue<LpQuote, QUEUE_CAPACITY>* queue,
                std::atomic<bool>* running);

    void start();   // spawns the thread
    void join();    // blocks until thread exits

    void run();

private:
    struct LpState {
        LpId         id;
        Price        mid;
        std::mt19937 rng;    // per-LP Random Number Generator, seeded from global_seed XOR lp_id
        int64_t      next_quote_ns;  // wall-clock time of next scheduled quote
        int          interval_ns;    // 1e9 / lp_quote_hz
    };

    const Config&                       cfg_;
    SPSCQueue<LpQuote, QUEUE_CAPACITY>* queue_;
    std::atomic<bool>*                  running_;
    std::vector<LpState>                lps_;
    std::thread                         thread_;
    LatencyBuffer<LpQuote, 256>         delay_buf_;
};

#endif // LP_SIMULATOR_H