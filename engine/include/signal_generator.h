#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H

#include "config.h"
#include "events.h"
#include "spsc_queue.h"
#include <atomic>
#include <random>
#include <thread>

class SignalGenerator {
public:
    SignalGenerator(const Config& cfg,
                    SPSCQueue<SignalUpdate, QUEUE_CAPACITY>* queue,
                    std::atomic<bool>* running);

    void start();
    void join();

private:
    void run();

    const Config&                            cfg_;
    SPSCQueue<SignalUpdate, QUEUE_CAPACITY>* queue_;
    std::atomic<bool>*                       running_;
    std::thread                              thread_;
};

#endif // SIGNAL_GENERATOR_H