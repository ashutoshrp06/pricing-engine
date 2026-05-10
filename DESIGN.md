# Design

## Instrument

The simulated instrument is a synthetic FX-like pair modelled on EUR/USD-style five-decimal pricing. Mid sits at 1.10000 (110000 internal pip-units), tick is 0.00001. All prices are `int64_t` throughout - no floats in the book, no floats in inventory. The architecture is instrument-agnostic; swap the constants and it prices anything.

## Pricing logic

On every event (LP quote update, signal update, or LT fill), the PE runs `reprice()`. The formula:

```
quote_mid  = lp_mid + α·signal - β·position
half_spread = base_spread · (1 + (1 - |signal|))

pe_bid = quote_mid - half_spread
pe_ask = quote_mid + half_spread
```

`lp_mid` is the mid computed from LP slots only, excluding PE's own quote. Using `mid()` (which includes PE) creates a feedback loop: PE's quote shifts the mid it uses to compute its next quote, causing unbounded drift. `lp_mid()` breaks that cycle.

The `α·signal` term is the directional component. When signal is positive, PE's mid shifts up, making the bid more competitive and the ask less. PE ends up buying more than selling, positioning long in the direction the signal points. When signal reverses, the inventory skew via `β·position` pulls the quote back.

The `β·position` term is inventory skew. A long position pushes quote_mid down, making the ask more competitive and pulling in sell-side LT flow to flatten inventory through fills rather than hedges. Fills earn spread. Hedges pay it. Getting this term strong enough that inventory self-corrects before the hedge threshold is the key to positive PnL.

The half-spread widens when `|signal|` is near zero (uncertain direction) and tightens when `|signal|` is near one (strong direction). At signal=0, half-spread is `2·base_spread`. At signal=±1, it is `base_spread`. This is a simple proxy for the classic spread-widening under uncertainty used in real market making, without any explicit model of adverse selection.

All prices are integer pip-units. The `α` and `β` terms use floating-point multiplication then round to the nearest integer tick. No floats are stored in the book or the inventory tracker. Floating-point prices accumulate rounding error across thousands of fills, and the error compounds in PnL calculations. Integer pip-units are exact. The instrument mid at 1.10000 maps to 110000 internally; one pip (0.00001) maps to 1 tick. All arithmetic stays in `int64_t`.

All RNGs across producers are seeded deterministically from a single global seed (`seed XOR producer_id`). This makes every run reproducible and is the reason the latency study can attribute result differences to injected latency rather than RNG variance. The seed is CLI-configurable; the default is 42.

## Hedge logic

When `|position| >= hedge_threshold`, PE crosses the spread to reduce exposure. It sells to the best bid LP when long, buys from the best ask LP when short. Hedge quantity targets a post-hedge residual of `hedge_threshold / 2`, not zero. This avoids immediately re-triggering the hedge. The remaining `hedge_threshold / 2` units are left for the quote skew to flatten through fills, which earn spread rather than paying it.

After every hedge, PE requotes immediately using the updated (post-hedge) position. Without this, the quote would reflect stale inventory for the next several events.

Hedge orders do not go through the LT queue. PE issues them directly to the book, simulating an outbound hedge to a specific LP. This is the same shape as a real PE sending a risk transfer to a named LP.

The hedge is a last-resort mechanism. With `beta=0.4`, a position of 60 units shifts quote_mid by 24 ticks. That is a 24-tick differential between best bid and best ask for the opposing side, which is large enough to attract LT flow and flatten inventory before the threshold is hit in most cases. The hedge fires when that fails, typically during a run of one-sided LT flow.

## Parameters

| Parameter | Default | What it controls |
|---|---|---|
| `alpha` | 0.1 | Directional bet per unit of signal |
| `beta` | 0.4 | Inventory skew per unit of position |
| `base_spread` | 1 tick | Minimum half-spread at maximum signal confidence |
| `hedge_threshold` | 60 units | Position size that triggers a cross-spread hedge |
| `num_lps` | 12 | Logical LP count |
| `lp_quote_hz` | 500 | Quote frequency per LP |
| `signal_hz` | 100 | Signal update frequency |
| `lt_arrival_hz` | 50 | LT order arrival rate |
| `seed` | 42 | RNG seed for all producers |

`alpha=0.1` keeps the directional component small. With a random-walk signal that has no predictive edge, large alpha means PE systematically buys above mid and sells below mid as the signal oscillates. At 0.1 tick per signal unit, the directional term is at most ±0.1 ticks - intentionally small so spread capture dominates PnL rather than signal noise.

`beta=0.4` was tuned empirically. Too low and inventory runs to the hedge threshold frequently, paying spread on every hedge. Too high and the quote skew pushes PE's prices so far from LP consensus that LTs stop hitting PE entirely. At 0.4, inventory typically self-corrects in 5 to 15 fill events.

`hedge_threshold=60` is roughly 1.2 seconds of one-sided LT flow at 50 Hz. It gives the skew mechanism time to work before falling back to a hedge.

All parameters are configurable via CLI. None are hardcoded in the logic.

## Signal generator

The signal is a pure random walk in [-1, 1], clamped at ±1. It has no correlation with future mid movements.

A correlated signal is plausible but I deliberately left it out. The latency study measures how strategy behaviour degrades as wire delays increase. That measurement does not require the strategy to be profitable in absolute terms - it requires the direction of degradation to be detectable. A random signal produces that. Tuning a correlation coefficient that is predictive enough to produce a visible edge but not so strong it dominates the variance is a separate calibration problem, and one outside the scope of this work.

With a pure random signal, expected PnL at zero latency is near zero in the long run. Single 60-second runs produce small positive or negative PnL from path-dependent variance. The latency study then examines how the PnL distribution shifts under injected delay, not how much edge is preserved. Both are valid framings. The random-walk version is simpler to defend and requires no tuning.

## What is intentionally simple

The LP price model is a random walk. Real LPs model their own inventory and adjust quotes accordingly. The simulation does not. This is fine because LP behaviour is not what this simulation is measuring. 

Each LP's mid is clamped to ±15 pips of the initial value. Without the clamp, twelve independent random walks drift apart over a long run and the consolidated book eventually crosses (best bid above best ask), at which point matching against it stops being meaningful.

LP half-spreads are 2 to 4 pip units. PE's half-spread is 1 to 2. This is deliberate. PE has to be best on price often enough to win fills, and LP spreads have to leave PE room to undercut. With LPs as tight as PE, PE would rarely have the book and the latency study would have nothing to measure.

The LT order model is Poisson arrivals with random side and fixed size. Real LT flow is correlated, time-of-day dependent, and size-varying. None of that matters for the scalability and latency measurements this engine is built to demonstrate.

The matching model uses a uniform-random tiebreak when PE is tied at best with LPs. Real venues use price-time priority or pro-rata. The tiebreak is documented in ARCHITECTURE.md. It does not advantage or disadvantage PE in expectation.

The signal has no predictive content. This is a deliberate choice, not a limitation. See above.

Hedge orders are instantaneous fills at the current best LP price. In production, a hedge is an order sent to a specific LP with its own wire latency and fill uncertainty. Modelling that would add PE-to-LP latency as a fourth injection point. It is documented in LATENCY_STUDY.md as future work.

Position standard deviation uses Welford's online algorithm updated on every event. No vector of samples, no heap allocation, no post-run pass. One running mean and one running M2 in the PE thread, updated in three arithmetic operations per event.