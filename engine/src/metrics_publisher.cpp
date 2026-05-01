#include "metrics_publisher.h"
#include "json.hpp"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using json = nlohmann::json;

static constexpr int BACKLOG = 1;

MetricsPublisher::MetricsPublisher(const Config& cfg, const PricingEngine& pe)
    : cfg_(cfg), pe_(pe)
{}

MetricsPublisher::~MetricsPublisher() {
    running_.store(false);
    join();
}

void MetricsPublisher::start() {
    running_.store(true);
    thread_ = std::thread(&MetricsPublisher::run, this);
}

void MetricsPublisher::join() {
    if (thread_.joinable()) thread_.join();
}

void MetricsPublisher::run() {
    // Set up listening socket once. Reuse address so restarts don't hit TIME_WAIT.
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { std::perror("socket"); return; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(cfg_.publisher_port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind"); close(server_fd); return;
    }
    listen(server_fd, BACKLOG);

    int interval_us = 1'000'000 / cfg_.publisher_rate_hz;

    while (running_.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        struct timeval timeout{0, 200'000};
        int ready = select(server_fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue; // timeout or error, loop and check running_

        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        // Stream snapshots until client disconnects or we're told to stop.
        while (running_.load()) {
            MetricsSnapshot snap = read_snapshot(pe_.snapshot());

            json msg;
            msg["timestamp_ns"]        = snap.timestamp_ns;
            msg["position"]            = snap.position;
            msg["realised_pnl"]        = snap.realised_pnl;
            msg["unrealised_pnl"]      = snap.unrealised_pnl;
            msg["fill_count"]          = snap.fill_count;
            msg["lt_to_pe_count"]      = snap.lt_to_pe_count;
            msg["lt_to_lp_count"]      = snap.lt_to_lp_count;
            msg["fill_rate_per_sec"]   = snap.fill_rate_per_sec;
            msg["spread_capture_mean"] = snap.spread_capture_mean;
            msg["mid_price"]           = snap.mid_price;
            msg["pe_bid"]              = snap.pe_bid;
            msg["pe_ask"]              = snap.pe_ask;
            msg["best_bid"]            = snap.best_bid;
            msg["best_ask"]            = snap.best_ask;
            msg["latency_p50_ns"]   = snap.latency_p50_ns;
            msg["latency_p99_ns"]   = snap.latency_p99_ns;
            msg["latency_p99_9_ns"] = snap.latency_p99_9_ns;

            std::string line = msg.dump() + "\n";
            ssize_t sent = send(client_fd, line.c_str(), line.size(), MSG_NOSIGNAL);
            if (sent < 0) break; // client disconnected

            std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
        }

        close(client_fd);
    }

    close(server_fd);
}