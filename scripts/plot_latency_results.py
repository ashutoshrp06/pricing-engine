#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

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
    baseline = df[df.run_set == "baseline"][metric].values[0]

    for (run_set, label), color in zip(SETS.items(), COLORS):
        rows = df[df.run_set == run_set].copy()
        # use the varying latency column for x-axis
        if run_set == "lt_only":
            rows = rows.sort_values("lt_to_pe_latency_us")
            xs = [0] + list(rows["lt_to_pe_latency_us"].values)
        elif run_set == "pe_only":
            rows = rows.sort_values("pe_to_book_latency_us")
            xs = [0] + list(rows["pe_to_book_latency_us"].values)
        else:
            rows = rows.sort_values("lp_to_pe_latency_us")
            xs = [0] + list(rows["lp_to_pe_latency_us"].values)
        ys = [baseline] + list(rows[metric].values)
        ax.plot(xs, ys, marker="o", label=label, color=color)

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
plot_metric("position_std",       "Position std dev",      "Exposure versus Injected Latency",      "latency_exposure.png")