# pricing-engine

A market-making pricing engine that ingests high-frequency LP quotes and a directional signal, competes in a consolidated book to capture flow, skews quotes to manage inventory, and publishes a live dashboard. Built as a take-home for a Quant Developer role.

## Quickstart

Requirements: Docker 20.10+, Docker Compose 2.0+. Nothing else.

```bash
git clone https://github.com/ashutoshrp06/pricing-engine
cd pricing-engine
docker compose up
```

Open http://localhost:8501 in your browser. The dashboard should be live within 30 seconds.

To stop: `Ctrl+C`, then `docker compose down`.

## Native build (Linux / macOS)

Requires cmake 3.15+, gcc 13+, and Python 3.12+. See your OS package manager for installation.

**Terminal 1:**
```bash
cd engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/engine
```

**Terminal 2:**
```bash
cd dashboard
pip install -r requirements.txt
streamlit run app.py
```

Open http://localhost:8501.

## What it does

The simulated instrument is a synthetic FX-like pair (mid ~1.10000, tick 0.00001), modelled on EUR/USD-style five-decimal pricing. The engine runs 12 logical LPs configured to quote at 500 Hz each (6,000 quote updates/sec target; timer resolution caveats in BENCHMARKS.md), a signal generator producing a directional signal at 100 Hz, and a liquidity taker placing Poisson-arrival market orders at 50 Hz. The pricing engine consumes all three streams, maintains a consolidated top-of-book across all 12 LPs plus itself, quotes competitively to capture LT flow, skews its mid based on inventory, and hedges when exposure crosses a threshold.

The dashboard shows live PnL, position, fill rate, spread capture, latency percentiles, and the current consolidated book state. It refreshes at 5 Hz and stays stable for extended runs.

### Performance

Numbers from native M4 bare metal. See [BENCHMARKS.md](BENCHMARKS.md) for full methodology and Docker numbers.

| Metric | Native (M4) |
|---|---|
| Peak throughput (1s window) | 14,326 events/s |
| Latency p50 | 167-208 ns |
| Latency p99 | 375 ns - 7.7 us |
| Latency p99.9 | 20-47 us (OS scheduler noise on macOS) |

End-to-end latency is from event production timestamp to PE decision. p50 is stable across runs and reflects the actual hot-path cost. p99.9 swings run-to-run due to macOS scheduler preemption; on Linux with `chrt` the tail tightens. Docker latency is not reported as Rosetta emulation dominates the measurement at 1.2-1.4s per event.

## Design choices

A handful of choices that affect what the system can and cannot tell you. Each is defended in the linked doc.

- PE-as-quoter. PE is a participant in the consolidated book alongside the 12 LPs, quoting competitively to capture LT flow. The threading model and the consolidated book design follow from this. See [ARCHITECTURE.md](ARCHITECTURE.md).

- Signal has no predictive content. Pure random walk in [-1, 1] with no correlation to future mid moves. A correlated signal would mix two effects in the latency study: edge erosion under stale data and stale-quote losses. With a random signal, expected zero-latency PnL is path variance only and the latency effect is separable from any strategy edge. Defended in [DESIGN.md](DESIGN.md).

- LT has no adverse-selection logic. Side chosen uniformly at random; the LT does not compare PE's quote against LP consensus or preferentially hit stale quotes. This is the largest gap between this simulation and real market behaviour and is why the LP-to-PE PnL prediction did not hold. Discussed in [LATENCY_STUDY.md](LATENCY_STUDY.md).

- Three latency-injection points, not four. LP-to-PE, LT-to-PE, PE-to-book. The signal-to-PE link is intentionally not injected because the signal is in-process. A fourth axis would be added if the signal came from a remote feed.

- Strategy parameters are hand-tuned, not optimised. `alpha`, `beta`, and `hedge_threshold` were picked by observation rather than calibrated against an objective. The strategy works well enough to exercise the simulation under latency stress, which is what the experiment needs.

## Latency study

PE-to-book is the dominant latency link. PnL falls from ~1450 to ~430 pip-units as PE-to-book delay increases from 0 to 10ms. Stale quote updates stay live in the book longer, LTs hit them adversely, and spread capture collapses. LP-to-PE latency shows a counterintuitive PnL rise, which is a simulation artifact: the LT has no adverse selection logic, so stale LP quotes do not hurt PE. LT-to-PE shows no clear trend. Full methodology, plots, and discussion in [LATENCY_STUDY.md](LATENCY_STUDY.md).

## Configuration

All parameters are CLI flags. Defaults:

```
--num-lps 12
--lp-quote-hz 500
--signal-hz 100
--lt-arrival-hz 50
--seed 42
--alpha 0.1
--beta 0.4
--base-spread 1
--hedge-threshold 60
--lp-to-pe-latency-us 0
--lt-to-pe-latency-us 0
--pe-to-book-latency-us 0
--duration 0          # 0 = run indefinitely
--publisher-port 8765
```

To run the engine directly (outside Docker) with custom latency:

```bash
./engine/build/engine --lp-to-pe-latency-us 1000 --duration 60
```

To re-run the latency sweep or regenerate plots, the scripts need `pandas` and `matplotlib`:

```bash
pip install pandas matplotlib
bash scripts/run_latency_sweep.sh
python3 scripts/plot_latency_results.py
```

## Platform notes

Images are built for `linux/amd64`. On Apple Silicon they run under emulation, which works but is slower to build. To rebuild natively for your local architecture:

```bash
docker compose build --no-cache
docker compose up
```

If port 8501 or 8765 is already in use:

```bash
DASHBOARD_PORT=8502 ENGINE_PORT=8766 docker compose up
```

## Repository layout

```
engine/          C++17 pricing engine
  src/           source files
  include/       headers
  bench/         throughput and latency benchmarks
dashboard/       Streamlit dashboard
scripts/         latency sweep and plot scripts
docs/            plots, CSV results, diagrams
```

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - threading model, data flow, component design, IPC
- [DESIGN.md](DESIGN.md) - pricing logic, hedge logic, parameter rationale
- [LATENCY_STUDY.md](LATENCY_STUDY.md) - methodology, results, discussion
- [BENCHMARKS.md](BENCHMARKS.md) - throughput and latency numbers, reproduction steps
- [AI_USAGE.md](AI_USAGE.md) - how AI tools were used during development

## Related work

The SPSC queue is adapted from [lob-simulator-optimized](https://github.com/ashutoshrp06/lob-simulator-optimized) (32.9 ns add latency, 6.4 ns SPSC enqueue). The consolidated book is a new top-of-book aggregator, not a matching engine. The feed handler pattern (one thread driving multiple logical LPs) is informed by [feed-parser](https://github.com/ashutoshrp06/feed-parser).