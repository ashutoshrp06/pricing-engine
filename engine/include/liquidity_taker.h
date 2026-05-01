#ifndef LIQUIDITY_TAKER_H
#define LIQUIDITY_TAKER_H

#include "config.h"
#include "events.h"
#include "spsc_queue.h"
#include "latency_buffer.h"
#include <atomic>
#include <random>
#include <thread>

class LiquidityTaker {
public:
    LiquidityTaker(const Config& cfg,
                   SPSCQueue<LtOrder, QUEUE_CAPACITY>* queue,
                   std::atomic<bool>* running);

    void start();
    void join();

    void run();


private:

    const Config&                        cfg_;
    SPSCQueue<LtOrder, QUEUE_CAPACITY>*  queue_;
    std::atomic<bool>*                   running_;
    std::thread                          thread_;
    LatencyBuffer<LtOrder, 256>          delay_buf_;
};

#endif // LIQUIDITY_TAKER_H