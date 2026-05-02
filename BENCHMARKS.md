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

Both sets of numbers are included: native arm64 (bare metal) and inside the linux/amd64 Docker container under Rosetta. The Docker numbers are what one on an x86_64 Linux would see from a native build.

## Throughput

`engine/bench/bench_throughput.cpp`. Runs the engine at 12 LPs x 1000 Hz, signal 1000 Hz, LT 1000 Hz for 60 seconds. The PE consumer drains all three queues in a tight round-robin loop. Events drained per second are sampled at 10 Hz and bucketed into 1-second windows.

Target: 14,000 events/s (12,000 LP + 1,000 SG + 1,000 LT).

| Metric | Native (M4) | Docker (linux/amd64) |
|---|---|---|
| Sustained throughput | 14,511 events/s | 14,139 events/s |
| Peak throughput (1s window) | 14,672 events/s | 14,224 events/s |
| Queue overflow events | 0 | 0 |

Engine sustains above-target throughput in both environments with no queue overflow. The ~2.5% drop in Docker is Rosetta overhead. In both cases the bottleneck is the producers, not the consumer. At default load (6,150 events/s), there is significant headroom before the consumer saturates.

## End-to-end Latency

`engine/bench/bench_latency.cpp`. Default load, 60 seconds, all injected latencies at 0. Every event is stamped at production; the PE records a consumption timestamp after finishing that event. The delta is the end-to-end latency for that event. Stored in a 2,048-slot ring buffer.

Percentiles explained: p50 is the median, half of events are faster than this. p99 means 99% of events completed within this time, 1% took longer. p99.9 covers all but the worst 0.1%.

| Metric | Native (M4) | Docker (linux/amd64) |
|---|---|---|
| p50 | 167 ns | 167 ns |
| p99 | 375 ns | 1,875 ns |
| p99.9 | 3,125 ns | 4,666 ns |
| max | 10,375 ns | 22,958 ns |
| Sample size | ~2,048 | ~2,048 |

p50 is identical in both environments. The hot path cost is fixed: SPSC drain, consolidated book update, pricing logic, inventory skew. That comes to 167 ns regardless of platform. The p99 and tail diverge because macOS and Rosetta do not offer real-time scheduling. These spikes are OS preemption, not hot-path regressions. On a Linux host with `taskset` the tail would tighten.

## Reproduction

```bash
cd engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bench_throughput
./build/bench_latency
```

Inside Docker:

```bash
docker compose build engine
docker compose run --rm --entrypoint ./bench_throughput engine
docker compose run --rm --entrypoint ./bench_latency engine
```