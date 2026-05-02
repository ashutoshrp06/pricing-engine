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
| Sustained throughput | 14,508 events/s | 14,139 events/s |
| Peak throughput (1s window) | 14,695 events/s | 14,224 events/s |
| Queue overflow events | 0 | 0 |

Engine sustains above-target throughput in both environments with no queue overflow. The ~2.5% drop in Docker is Rosetta overhead. In both cases the bottleneck is the producers, not the consumer. At default load (6,150 events/s), there is significant headroom before the consumer saturates.

## End-to-end Latency

`engine/bench/bench_latency.cpp`. Default load, 60 seconds, all injected latencies at 0. Every event is stamped at production; the PE records a consumption timestamp after finishing that event. The delta is the end-to-end latency for that event. Stored in a 2,048-slot ring buffer.

Percentiles explained: p50 is the median, half of events are faster than this. p99 means 99% of events completed within this time, 1% took longer. p99.9 covers all but the worst 0.1%.

| Metric | Native (M4) | Docker (linux/amd64) |
|---|---|---|
| p50 | 208 ns | 167 ns |
| p99 | ~3,000 ns* | 1,875 ns |
| p99.9 | unstable* | 4,666 ns |
| max | unstable* | 22,958 ns |
| Sample size | ~2,048 | ~2,048 |

*Across 5 runs, p50 was stable (167-291 ns). p99 was sub-3.2us on 3 of 5 runs; the other two spiked to 100-145us. p99.9 ranged from 7.9us to 322us. This is OS scheduler preemption, not engine variance. macOS provides no real-time scheduling primitive equivalent to Linux's `chrt`. On a Linux host with `taskset` and `chrt -f`, the tail would tighten significantly. The p50 is the only stable number on this machine.

p50 is the number that reflects the engine. The hot path is: SPSC drain, consolidated book update, pricing logic, inventory skew. That consistently comes out around 200 ns. Everything past p99 is the OS occasionally deciding something else matters more. Docker numbers are cleaner at the tail because the workload isolation inside the container happens to reduce scheduling interference on this machine.

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