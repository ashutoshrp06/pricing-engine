#include "lp_simulator.h"
#include <chrono>

// Mid price at startup: 1.10000 in FX terms = 110000 internal pips
static constexpr Price INITIAL_MID = 110000;
// How far the mid walk can step per quote, in pips
static constexpr int MID_WALK_STEP = 1;
static constexpr int MID_WALK_CLAMP = 3;
// Half-spread around mid, in pips. Base is 1 pip each side
static constexpr int BASE_HALF_SPREAD = 5;

using Clock = std::chrono::steady_clock;

static int64_t now_ns() {
    return Clock::now().time_since_epoch().count();
}

LpSimulator::LpSimulator(const Config& cfg,
                         SPSCQueue<LpQuote, QUEUE_CAPACITY>* queue,
                         std::atomic<bool>* running)
    : cfg_(cfg), queue_(queue), running_(running)
{
    int64_t t = now_ns();
    int interval_ns = 1'000'000'000 / cfg_.lp_quote_hz;

    lps_.reserve(cfg_.num_lps);
    for (int i = 0; i < cfg_.num_lps; ++i) {
        LpState s;
        s.id            = i + 1;           // LP ids start at 1; 0 is reserved for PE
        s.mid           = INITIAL_MID;
        s.rng           = std::mt19937(cfg_.seed ^ static_cast<uint64_t>(i + 1));
        s.interval_ns   = interval_ns;
        // stagger initial quote times so all 12 LPs don't fire simultaneously on tick 1
        s.next_quote_ns = t + (i * interval_ns / cfg_.num_lps);
        lps_.push_back(std::move(s));
    }
}

void LpSimulator::start() {
    thread_ = std::thread(&LpSimulator::run, this);
}

void LpSimulator::join() {
    if (thread_.joinable()) thread_.join();
}

void LpSimulator::run() {
    std::uniform_int_distribution<int> walk_dist(-MID_WALK_STEP, MID_WALK_STEP);
    std::uniform_int_distribution<int> spread_dist(0, 2); // small per-quote spread variation

    while (*running_) {
        int64_t t = now_ns();

        for (auto& lp : lps_) {
            if (t < lp.next_quote_ns) continue;

            // Advance mid with a bounded random walk
            int step = walk_dist(lp.rng);
            // Clamp mid so it doesn't drift unboundedly far from INITIAL_MID
            Price new_mid = static_cast<Price>(
                std::max(static_cast<int64_t>(INITIAL_MID - MID_WALK_CLAMP),
                std::min(static_cast<int64_t>(INITIAL_MID + MID_WALK_CLAMP),
                         static_cast<int64_t>(lp.mid) + step)));
            lp.mid = new_mid;

            int half_spread = BASE_HALF_SPREAD + spread_dist(lp.rng);

            LpQuote q;
            q.lp_id                = lp.id;
            q.bid                  = lp.mid - static_cast<Price>(half_spread);
            q.ask                  = lp.mid + static_cast<Price>(half_spread);
            q.production_timestamp = t;

            queue_->push(q);

            lp.next_quote_ns += lp.interval_ns;
        }
    }
}