# Benchmarks

## Environment

| Property | Value |
|---|---|
| CPU | Apple M4 (10 logical cores) |
| OS | macOS 26.4.1 (Build 25E253) |
| Compiler (native) | Apple Clang 21.0.0 |
| Compiler (Docker) | GCC 13.4.0 |
| Docker image platform | linux/amd64 (Rosetta emulation on M4) |
| Optimisation flags | `-O3 -march=native` |
| Docker | 29.2.1 |

Native arm64 numbers are the primary benchmark. Docker throughput is included for reference; Docker latency is not reported because Rosetta emulation dominates the measurement.

## Throughput

`engine/bench/bench_throughput.cpp`. Runs the engine at 12 LPs x 1000 Hz, signal 1000 Hz, LT 1000 Hz for 60 seconds. The PE consumer drains all three queues in a tight round-robin loop. Events drained per second are sampled at 10 Hz and bucketed into 1-second windows.

Target: 14,000 events/s (12,000 LP + 1,000 SG + 1,000 LT).

| Metric | Native (M4) | Docker/Rosetta (linux/amd64) |
|---|---|---|
| Sustained throughput (median) | ~4,900 events/s | N/A |
| Peak throughput (1s window) | 14,326 events/s | 9,977 events/s |
| Queue overflow events | 0 | 0 |

Peak throughput confirms the consumer can handle the target load when producers deliver at the configured rate. Sustained median is low on macOS because `sleep_for` resolves to 5-15ms on Darwin, so producers pace at ~100-200 Hz instead of 1000 Hz. Most 1-second buckets see ~4,900 events; burst seconds where timers fire accurately hit 14,300+. On Linux, `nanosleep` resolves at ~100us and sustained would track peak. Docker peak is lower because Rosetta adds emulation overhead. No queue overflow in either case.

## End-to-end Latency

`engine/bench/bench_latency.cpp`. Default load, 120 seconds, all injected latencies at 0. Every event is stamped at production; the PE records a consumption timestamp after finishing that event. The delta is the end-to-end latency for that event. Stored in a 16,384-slot ring buffer.

Percentiles explained: p50 is the median, half of events are faster than this. p99 means 99% of events completed within this time, 1% took longer. p99.9 covers all but the worst 0.1%.

| Metric | Native (M4) |
|---|---|
| p50 | 167-208 ns |
| p99 | 375 ns - 7.7 us |
| p99.9 | 20-47 us |
| max | 39-190 us |
| Sample size | ~16,384 |

p50 is stable across all 10 runs. That is the actual hot-path cost: SPSC drain, consolidated book update, pricing logic, inventory skew. p99.9 and max swing run-to-run because macOS has no real-time scheduling equivalent to Linux `chrt`. The spikes are OS preemption, not engine regressions. Docker latency is not reported - under Rosetta, thread scheduling latency dominates at 1.2-1.4s per event, which is emulation overhead, not engine behaviour.

## Reproduction

Native:

```bash
cd engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bench_latency
./build/bench_throughput
```

Docker:

```bash
docker compose run --rm --entrypoint /app/bench_latency engine
docker compose run --rm --entrypoint /app/bench_throughput engine
```

bench_latency: 12 LPs x 500 Hz, SG 100 Hz, LT 50 Hz, seed 42, zero injected latency, 120s.
bench_throughput: 12 LPs x 1000 Hz, SG 1000 Hz, LT 1000 Hz, seed 42, zero injected latency, 60s.