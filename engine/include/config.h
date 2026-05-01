#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

static constexpr size_t QUEUE_CAPACITY = 4096;

struct Config {
    int      num_lps               = 12;
    int      lp_quote_hz           = 500;
    int      signal_hz             = 100;
    int      lt_arrival_hz         = 50;
    int      duration_s            = 0;      // 0 = run until Ctrl-C
    uint64_t seed                  = 42;

    double   alpha                 = 0.1;    // ticks per signal unit
    double   beta                  = 0.4;    // ticks per inventory unit
    int      base_spread           = 1;      // ticks
    int      hedge_threshold       = 60;    // inventory units, was 20

    int      lp_to_pe_latency_us   = 0;
    int      lt_to_pe_latency_us   = 0;
    int      pe_to_book_latency_us = 0;
    int      publisher_port        = 8765;
    int      publisher_rate_hz     = 5;
};

inline Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc - 1; ++i) {
        std::string_view key(argv[i]);
        if      (key == "--num-lps")               cfg.num_lps                = std::atoi(argv[++i]);
        else if (key == "--lp-quote-hz")            cfg.lp_quote_hz           = std::atoi(argv[++i]);
        else if (key == "--signal-hz")              cfg.signal_hz             = std::atoi(argv[++i]);
        else if (key == "--lt-arrival-hz")          cfg.lt_arrival_hz         = std::atoi(argv[++i]);
        else if (key == "--duration")               cfg.duration_s            = std::atoi(argv[++i]);
        else if (key == "--seed")                   cfg.seed                  = std::strtoull(argv[++i], nullptr, 10);
        else if (key == "--alpha")                  cfg.alpha                 = std::atof(argv[++i]);
        else if (key == "--beta")                   cfg.beta                  = std::atof(argv[++i]);
        else if (key == "--base-spread")            cfg.base_spread           = std::atoi(argv[++i]);
        else if (key == "--hedge-threshold")        cfg.hedge_threshold       = std::atoi(argv[++i]);
        else if (key == "--lp-to-pe-latency-us")    cfg.lp_to_pe_latency_us   = std::atoi(argv[++i]);
        else if (key == "--lt-to-pe-latency-us")    cfg.lt_to_pe_latency_us   = std::atoi(argv[++i]);
        else if (key == "--pe-to-book-latency-us")  cfg.pe_to_book_latency_us = std::atoi(argv[++i]);
        else if (key == "--publisher-port")         cfg.publisher_port        = std::atoi(argv[++i]);
        else {
            std::cerr << "Unknown arg: " << key << "\n";
            std::exit(1);
        }
    }
    return cfg;
}

#endif // CONFIG_H