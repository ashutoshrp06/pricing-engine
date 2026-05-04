#!/usr/bin/env python3
import pandas as pd # type: ignore
import matplotlib.pyplot as plt # type: ignore
import matplotlib.ticker as ticker # type: ignore
import numpy as np # type: ignore

CSV = "docs/latency_results.csv"
df = pd.read_csv(CSV)

SETS = {
    "lp_only":  "LP-to-PE only",
    "lt_only":  "LT-to-PE only",
    "pe_only":  "PE-to-book only",
    "combined": "All three combined",
}
COLORS = ["#2196f3", "#4caf50", "#ff9800", "#e91e63"]
LATENCIES = [0, 100, 500, 1000, 5000, 10000]

def get_series(run_set, metric):
    baseline = df[df.run_set == "baseline"][metric].values[0]
    rows = df[df.run_set == run_set].sort_values("lp_to_pe_latency_us" if run_set != "lt_only" else "lt_to_pe_latency_us")
    return [0] + list(rows[metric].values), [baseline] + list(rows[metric].values)

def plot_metric(metric, ylabel, title, filename):
    fig, ax = plt.subplots(figsize=(9, 5))

    # baseline: mean across seeds
    baseline_rows = df[df.run_set == "baseline"]
    baseline = baseline_rows[metric].mean()

    for (run_set, label), color in zip(SETS.items(), COLORS):
        rows = df[df.run_set == run_set].copy()

        if run_set == "lt_only":
            lat_col = "lt_to_pe_latency_us"
        elif run_set == "pe_only":
            lat_col = "pe_to_book_latency_us"
        else:
            lat_col = "lp_to_pe_latency_us"

        grouped = rows.groupby(lat_col)[metric].agg(["mean", "min", "max"]).reset_index()
        grouped = grouped.sort_values(lat_col)

        xs   = [0] + list(grouped[lat_col].values)
        mean = [baseline] + list(grouped["mean"].values)
        lo   = [baseline] + list(grouped["min"].values)
        hi   = [baseline] + list(grouped["max"].values)

        ax.plot(xs, mean, marker="o", label=label, color=color)
        ax.fill_between(xs, lo, hi, alpha=0.15, color=color)

    ax.set_xscale("symlog", linthresh=50)
    ax.set_xlabel("Injected latency (us)")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x)}"))
    plt.tight_layout()
    plt.savefig(f"docs/{filename}", dpi=150)
    plt.close()
    print(f"Saved docs/{filename}")

plot_metric("pnl_total",          "Total PnL (pip-units)", "PnL versus Injected Latency",          "latency_pnl.png")
plot_metric("fill_rate",          "Fill rate (fills/sec)", "Fill Rate versus Injected Latency",     "latency_fill_rate.png")
plot_metric("position_std",       "Position std dev (units)",      "Exposure versus Injected Latency",      "latency_exposure.png")