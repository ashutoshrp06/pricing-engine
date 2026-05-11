# Architecture

## Overview

Single C++ process, five threads. Three producer threads push events into three SPSC queues. The PE consumer thread drains all three, maintains the consolidated book, runs pricing and hedge logic, and writes a metrics snapshot. A fifth thread reads that snapshot and publishes it over TCP at 5 Hz. The Streamlit dashboard connects to that port and renders the live view. Each shape choice (thread count, queue topology, single-process model, IPC mechanism) is defended in the section that introduces it.

```
┌──────────────────────────────────────────┐
│           LP Simulator Thread            │
│  12 logical LPs  ·  LP-to-PE delay buf   │
└────────────────────┬─────────────────────┘
                     │ LpQuote (SPSC)
                     │ ◄─ SignalUpdate (SPSC)  ──  Signal Generator  (random walk)
                     │ ◄─ LtOrder (SPSC)  ─────────  LT Thread  (Poisson arrivals)
                     │
                     ▼
┌──────────────────────────────────────────┐
│            PE Consumer Thread            │
│                                          │
│       Consolidated book (13 slots)       │
│  Pricing: mid + α·signal - β·inventory   │
│         PE-to-book delay buffer          │
│               Hedge logic                │
│             Inventory + PnL              │
│         Seqlock snapshot (write)         │
└────────────────────┬─────────────────────┘
                     │ seqlock read (5 Hz)
                     ▼
┌──────────────────────────────────────────┐
│         Metrics Publisher Thread         │
└────────────────────┬─────────────────────┘
                     │ TCP NDJSON · localhost:8765
                     ▼
┌──────────────────────────────────────────┐
│           Streamlit Dashboard            │
│              localhost:8501              │
└──────────────────────────────────────────┘
```

## Threading model

**LP simulator thread.** Drives all 12 logical LPs inside a single OS thread. Each LP has independent state: its own mid price (random walk), its own RNG (seeded as `global_seed XOR lp_id`), and its own next-quote timer. The thread visits each LP on every tick and emits a quote when that LP is due. Quotes go through an in-thread delay buffer before landing on the SPSC queue.

This is one thread, not 12. Real feed handlers work the same way: one thread receives market data for many sources and dispatches sequentially. Thread-per-LP would break the SPSC single-producer invariant or require 12 separate queues. The PE sees quotes tagged with 12 distinct `lp_id` values and the consolidated book keeps 12 independent bid/ask slots, so nothing is lost at the data level.

**Signal generator thread.** Random walk in [-1, 1], clamped at ±1. Emits `SignalUpdate` events at the configured frequency (default 100 Hz).

**Liquidity taker thread.** Poisson-arrival market order generator. Emits `LtOrder` events with side and size only. It does not touch the book; the PE does the matching when it dequeues the event.

**PE consumer thread.** The single linearization point for all events. Drains three SPSC queues in a round-robin tight loop, updates the consolidated book, reprices, runs hedge logic, writes a seqlock snapshot. No locks. No shared mutable state with any producer thread other than the SPSC ring buffers themselves.

**Metrics publisher thread.** Wakes at 5 Hz, reads the seqlock snapshot, serialises to NDJSON, writes to a TCP socket. If no client is connected, the snapshot is dropped silently. The publisher cannot block the PE thread. 5 Hz matches the dashboard refresh rate and is fast enough for a human watching live metrics; anything faster would just burn CPU serialising JSON nobody reads.

## SPSC queues

Three queues: one per producer thread. Each is a cache-line aligned ring buffer with 4096 slots and acquire/release ordering. The PE pops from all three in rotation.

Three SPSC queues instead of one MPSC because SPSC is simpler: one producer owns the write end, one consumer owns the read end, no coordination needed. MPSC needs either a lock or a more complex lock-free scheme to handle concurrent pushes. With only three producers that complexity buys nothing. The PE drains all three in the same round-robin loop it would use for one.

At default load (12 LPs at 500 Hz = 6000 events/s, SG at 100 Hz, LT at 50 Hz), aggregate input is roughly 6150 events/s. The PE drains at full CPU speed so queues stay near-empty under normal load. 4096 slots gives roughly 650ms of headroom on the LP queue (6000 events/s) if the consumer stalls; the SG and LT queues have proportionally more (40s and 81s respectively). Small enough that the full ring fits comfortably in L2 cache.

The engine is a single process. Multi-process would require shared memory or a socket for the consolidated book, complicate latency measurement (inter-process latency mixes with intra-engine latency), and add surface area to the Docker setup. Five threads and three SPSC queues is simple to reason about. Multi-process adds no benefit at this scale.

## Consolidated book

13 slots: index 0 for PE, indices 1-12 for LPs. Two `int64_t` arrays `bids_[13]` and `asks_[13]`. Zero means that participant is not yet quoting.

Best bid and best ask are recomputed on every update via a full 13-slot rescan. At 13 entries that is 13 comparisons, cheaper than maintaining a sorted structure.

`lp_mid()` computes mid from LP slots only (indices 1-12), excluding PE. The pricing formula uses this value. If PE's own quote fed into the mid it uses to reprice, you get a feedback loop that causes unbounded drift. `mid()` (all slots including PE) exists for display only.

## Latency injection

Three injection points, each an in-thread FIFO ring buffer:

| Link | CLI flag | Default |
|---|---|---|
| LP-to-PE | `--lp-to-pe-latency-us` | 0 |
| LT-to-PE | `--lt-to-pe-latency-us` | 0 |
| PE-to-book | `--pe-to-book-latency-us` | 0 |

Each item is stamped with `release_time = now + delay_ns` on entry and released when `now >= release_time`. Delay is constant so items exit in the same order they entered. A FIFO ring suffices; no priority queue needed.

The signal-to-PE link has no injection. The signal generator is in-process.

PE-to-book delay covers PE quote updates only. Hedge inventory adjustments are instantaneous; the post-hedge requote goes through the delay buffer. In production, hedge orders would share the same outbound wire and warrant a separate injection point, noted in LATENCY_STUDY.md as future work.

## Matching semantics

The PE thread owns the consolidated book and does all matching. When an LT order arrives:

1. If PE has unique best on the opposite side, PE wins the fill.
2. If PE is tied at best with N LPs, the tiebreak is uniform random using the PE's deterministic RNG. PE wins with probability 1 / (1 + N).
3. If PE is not at best, an LP wins. PE is unaffected.

Because matching runs on a single thread with no shared book state, there are no races. An LP quote that arrives 1ms after an LT order will not affect that order's match, because the injected latency means the PE had not yet seen that quote when it processed the LT event. That is the point of the latency injection model.

## Metrics snapshot

`MetricsSnapshot` is a plain struct (~120 bytes) holding all published fields: PnL, position, fill counts, spread capture, latency percentiles (p50, p99, p99.9, max), and position standard deviation computed via Welford's online algorithm on every event.

The PE writes via a seqlock: increment a counter (odd = write in progress), write fields, increment again (even = done). The publisher reads the counter twice and retries if it changed or was odd. No lock on the PE hot path.

## IPC: TCP NDJSON

The publisher listens on localhost:8765 and writes one JSON object per line at 5 Hz. On client disconnect it accepts the next connection. No buffering; drops are silent.

TCP NDJSON over shared memory or a Unix socket because it is debuggable with `nc localhost 8765`, needs no shared volume in Docker, and keeps engine and dashboard fully decoupled. Throttling is on the engine side; the dashboard cannot overload the engine regardless of how often it polls. Prices in the snapshot are raw pip-unit integers; the dashboard converts to decimal on display via `price / 100000.0`.

## Docker

The engine uses a multi-stage build pinned to `linux/amd64`. The engine image builds on `gcc:13-bookworm` and runs on `ubuntu:24.04`. `debian:bookworm-slim` was the first choice for the runtime stage but ships with `libstdc++6` from gcc 12, which is missing `GLIBCXX_3.4.32` required by a gcc 13 binary. `ubuntu:24.04` ships gcc 13's libstdc++ and resolves the mismatch cleanly without copying .so files or static linking. The dashboard is a single-stage Python image, also pinned to `linux/amd64` and uses `python:3.12-slim`. Pinning ensures the images run on x86_64 reviewer machines regardless of build host architecture.

The engine sources compile into `engine_lib`, a static library linked by three targets: the `engine` binary, `bench_throughput`, and `bench_latency`. Without this, bench sources would need to re-list every engine source file or the bench targets would get stale builds when engine sources change. The static library keeps CMakeLists clean and ensures the bench binaries always use the same compiled objects as the engine.

The dashboard reaches the engine via the Docker bridge network using the service hostname `engine`. `ENGINE_HOST` and `ENGINE_PORT` env vars let you override both without rebuilding.