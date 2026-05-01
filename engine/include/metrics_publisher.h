#pragma once
#include "pricing_engine.h"
#include "config.h"
#include <atomic>
#include <thread>

// Reads PE snapshot at 5 Hz, serialises to NDJSON, writes to TCP localhost.
// If no client is connected, snapshots are dropped silently.
class MetricsPublisher {
public:
    MetricsPublisher(const Config& cfg, const PricingEngine& pe);
    ~MetricsPublisher();

    void start();
    void join();

private:
    void run();

    const Config&         cfg_;
    const PricingEngine&  pe_;
    std::atomic<bool>     running_{false};
    std::thread           thread_;
};

inline MetricsSnapshot read_snapshot(const SequencedSnapshot& s) {
    while (true) {
        uint64_t seq1 = s.seq.load(std::memory_order_acquire);
        if (seq1 & 1) continue;
        MetricsSnapshot snap = s.data;
        uint64_t seq2 = s.seq.load(std::memory_order_acquire);
        if (seq1 == seq2) return snap;
    }
}