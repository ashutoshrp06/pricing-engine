#include "config.h"
#include "events.h"
#include "spsc_queue.h"
#include "lp_simulator.h"
#include "signal_generator.h"
#include "liquidity_taker.h"
#include "pricing_engine.h"
#include "metrics_publisher.h"

#include <atomic>
#include <thread>
#include <csignal>
#include <cstdio>
#include <chrono>

static std::atomic<bool> running{true};

static void on_signal(int) {
    running.store(false, std::memory_order_relaxed);
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    SPSCQueue<LpQuote,     QUEUE_CAPACITY> lp_queue;
    SPSCQueue<SignalUpdate, QUEUE_CAPACITY> sg_queue;
    SPSCQueue<LtOrder,     QUEUE_CAPACITY> lt_queue;

    LpSimulator      lp_sim(cfg, &lp_queue, &running);
    SignalGenerator  sig_gen(cfg, &sg_queue, &running);
    LiquidityTaker   lt(cfg, &lt_queue, &running);
    PricingEngine    pe(cfg, &lp_queue, &sg_queue, &lt_queue);
    MetricsPublisher publisher(cfg, pe);

    std::thread lp_thread([&]{ lp_sim.run(); });
    std::thread sg_thread([&]{ sig_gen.run(); });
    std::thread lt_thread([&]{ lt.run(); });
    std::thread pe_thread([&]{ pe.run(running); });

    publisher.start();

    if (cfg.duration_s > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(cfg.duration_s));
        running.store(false, std::memory_order_relaxed);
    }

    lp_thread.join();
    sg_thread.join();
    lt_thread.join();
    pe_thread.join();

    auto snap = pe.inventory().snapshot();
    std::printf("\n--- Phase 3 Summary ---\n");
    std::printf("Fill count (PE)  : %lld\n",  snap.fill_count);
    std::printf("Fill count (LP)  : %lld\n",  pe.lt_to_lp_count());
    std::printf("PE fill share    : %.1f%%\n",
        (snap.fill_count + pe.lt_to_lp_count()) > 0
        ? 100.0 * snap.fill_count / (snap.fill_count + pe.lt_to_lp_count())
        : 0.0);
    std::printf("Hedge count      : %lld\n",  snap.hedge_count);
    std::printf("Final position   : %lld\n",  snap.position);
    std::printf("Realised PnL     : %lld pip-units\n", snap.realised_pnl);
    std::printf("Unrealised PnL   : %lld pip-units\n", snap.unrealised_pnl);
    std::printf("Spread cap mean  : %.2f pip-units\n", snap.spread_capture_mean);
    std::printf("Peak abs position: %lld\n", snap.position_peak_abs);

    return 0;
}