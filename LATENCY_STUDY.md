# Latency Study

## Question

What happens to strategy PnL, fill rate, and inventory exposure when wire latency is introduced between components? Which link hurts most, and does the result match the prior?

## Hypothesis

Before running the sweep:

1. **LP-to-PE latency** makes PE quote on a stale market view. Other participants hit PE when its price is wrong in their favour. PnL should fall.

2. **LT-to-PE latency** makes PE's view of its own fills lag. Hedge decisions fire on stale positions. Exposure variance should rise, PnL should fall.

3. **PE-to-book latency** makes quote updates land late. Stale quotes stay visible and get hit adversely. PnL should fall.

4. **Fill rate** direction is not obvious in advance.

5. **Exposure variance** should rise on every link, since delay anywhere makes either the hedge input or the hedge delivery stale.

## Methodology

105 runs across five sets: baseline (all latencies at 0), LP-to-PE only, LT-to-PE only, PE-to-book only, all three combined. Latency values: 0, 100, 500, 1000, 5000, 10000 us. Each cell ran 5 times with seeds 42-46, 60 seconds per run. Lines in the plots show mean across seeds; shaded bands show min to max. Only latency varies between runs.

Hedges did not trigger in any run at default parameters (hedge_threshold=60, beta=0.4). The exposure analysis reflects quote-skew dynamics only.

## Results

![PnL versus Injected Latency](docs/latency_pnl.png)

PE-to-book is the only link with a clear PnL signal. PnL sits at 1511 pip-units at baseline, drops to 1390 at 100us, 1342 at 500us, 1235 at 1ms, 759 at 5ms, and 376 at 10ms. The drop is monotonic. Combined tracks PE-to-book closely up to 1ms then falls further, reaching 231 at 10ms. LP-to-PE shows no degradation - mean PnL ranges 1409 to 1548 across the tested latencies with no directional trend. LT-to-PE is also flat - means range from 1416 to 1537, all within seed variance of baseline.

![Fill Rate versus Injected Latency](docs/latency_fill_rate.png)

Fill rate is flat across all runs and all links. The full range is 31.2 to 31.9 fills/sec. PE-to-book and combined show a marginal drop at 10ms (31.31 and 31.17 respectively) but it is within seed variance. No link moves fill rate meaningfully.

![Exposure versus Injected Latency](docs/latency_exposure.png)

PE-to-book drives exposure up clearly above 1ms. Position std dev sits at 6.00 at baseline, rises gradually to 6.04 at 500us and 6.11 at 1ms, then more sharply to 6.65 at 5ms and 7.09 at 10ms. Combined follows the same curve. LP-to-PE and LT-to-PE stay close to baseline throughout, ranging 5.80 to 6.03, with no upward trend.

## Discussion

**Prediction 1 (LP-to-PE degrades PnL): wrong.** PnL stayed flat around baseline rather than degrading. The reason is that the LT in this simulation has no adverse selection logic. A real liquidity taker would compare PE's stale quote against current LP quotes and hit PE only when the mispricing is in its favour. Here, the LT fires market orders without inspecting staleness, so the mispricing never gets exploited. Stale LP quotes shift PE's mid reference around, but the resulting quote misalignment hits direction-blind LT flow, so PnL effects are noise-dominated rather than systematically negative. This is a simulation gap, not a real-world result, and is noted in the limitations.

**Prediction 2 (LT-to-PE degrades PnL and raises exposure): wrong.**
LT-to-PE shows a weak downward drift at high latency but the seed variance is large enough that the signal is not reliable, and exposure slightly decreased at high latency. When fill notifications arrive late, PE's beta skew reacts to inventory changes after a delay, which means PE quotes closer to mid for longer before the skew kicks in. That counterintuitively reduces position variance compared to aggressive immediate skewing. This is a second-order effect of the specific parameter values, not a general result.

**Prediction 3 (PE-to-book degrades PnL): correct.**
The most robust result in the study. Stale PE quotes stay visible after PE would have pulled or repriced them. The LT hits them. PnL falls from 1511 to 376 pip-units across the tested range, monotonically. This is the stale-quote problem in market making and it shows up clearly.

**Prediction 4 (fill rate direction ambiguous): correct.**
Fill rate is flat. Stale quotes attract slightly more fills while PE's wider effective spread from delayed repricing slightly reduces LT hits. These effects cancel at these latency scales.

**Prediction 5 (exposure rises on every link): wrong for two of three links.**
PE-to-book raised exposure as predicted, clearly and monotonically above 1ms. Position std dev sits at 6.00 at baseline, rises gradually to 6.04 at 500us and 6.11 at 1ms, then more sharply to 6.65 at 5ms and 7.09 at 10ms. Hedges did not trigger in any of the 105 runs at default parameters - beta=0.4 keeps inventory self-correcting well below the 60-unit threshold through quote skew alone. The exposure rise under PE-to-book latency is therefore driven by quote-skew breakdown: stale PE quotes remain best for longer, attract one-sided LT flow before the skew can adjust, and accumulate position. LP-to-PE and LT-to-PE latency don't have this effect because they don't change how long PE's quote sits visible in the book.

The main result: PE-to-book latency is the most damaging link in this simulation. Quote update latency to the venue is the primary latency risk in any market-making system, and this study reproduces that.

## Limitations

- 60 seconds per run with 5 seeds. Run-to-run variance is visible in the bands, particularly for PnL and fill rate. The PE-to-book and combined exposure results are tight enough to be reliable; the LP-to-PE PnL signal is not.
- Single machine. Numbers are not portable across CPUs.
- The LT has no adverse selection logic. This is the largest gap between this simulation and real market behaviour. It masks the expected LP-to-PE degradation entirely.
- LP prices follow a deterministic random walk with no correlation to signal. Spread capture at zero latency is path variance, not edge.
- LT arrivals are a simple Poisson process with no latency-dependent behaviour.
- Tiebreak at best price uses uniform random allocation. Price-time priority was considered but not implemented; at the LP update frequencies used here (500 Hz per LP), timestamps would be near-identical and pro-rata would be more realistic than price-time anyway.
- 60-second runs may not capture rare inventory regimes. A strategy that accumulates position slowly could behave differently over a longer window.

## Future Work

The current model applies one constant latency per link, shared across all LPs. A more realistic model would assign heterogeneous per-LP latency profiles (LP1 at 50us, LP2 at 2ms) to study how PE behaves when some feeds are fresh and others are stale. This was scoped as a third-tier feature and not built for this submission.

A second extension is to model PE-to-LP wire latency for outbound hedge orders as a fourth injection point. The current engine applies PE-to-book delay to quote updates only; hedge inventory adjustments are immediate, which collapses two distinct production paths into one parameter. Splitting them would let the study separate quote-staleness cost from hedge-delivery cost.