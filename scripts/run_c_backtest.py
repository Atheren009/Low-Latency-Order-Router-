#!/usr/bin/env python3
"""
run_c_backtest.py — Python analytics bridge for BOTH C backtest outputs.

Modes:
  --mode real    → reads c_backtest_results.csv (real OHLCV data, sliding window)
  --mode sim     → reads c_sim_results.csv      (synthetic orders, varying size/side)
  --mode both    → runs and charts both (default)

Steps for each mode:
  1. Optionally build the C binary (cmake + make).
  2. Optionally run the C backtest/sim binary.
  3. Load results CSV with pandas.
  4. Generate premium dark-mode charts.

Usage:
  cd ~/Order-Router
  python scripts/run_c_backtest.py                          # full run, both modes
  python scripts/run_c_backtest.py --max-windows 1000       # limit real-data windows
  python scripts/run_c_backtest.py --skip-build --skip-run  # charts only
  python scripts/run_c_backtest.py --mode sim               # sim orders only
"""
import argparse
import subprocess
import sys
from pathlib import Path

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np

ROOT        = Path(__file__).parent.parent
C_DIR       = ROOT / "c"
BUILD_DIR   = C_DIR / "build"
FEED_DIR    = ROOT / "Price Feed"
RESULTS_DIR = ROOT / "results"
CHART_DIR   = RESULTS_DIR / "c_charts"
OUT_REAL    = RESULTS_DIR / "c_backtest_results.csv"
OUT_SIM     = RESULTS_DIR / "c_sim_results.csv"

PALETTE = {
    "BestPrice": "#4C6EF5",
    "Smart":     "#37B679",
    "TWAP(5)":   "#F59E0B",
    "VWAP(5)":   "#EF4444",
}
STRATEGY_ORDER = ["BestPrice", "Smart", "TWAP(5)", "VWAP(5)"]
DARK_BG  = "#0F172A"
PANEL_BG = "#1E293B"
GRID_COL = "#334155"


# ── Helpers ──────────────────────────────────────────────────────────────────

def _style(ax):
    ax.set_facecolor(PANEL_BG)
    ax.tick_params(colors="white")
    for sp in ax.spines.values():
        sp.set_color(GRID_COL)
    ax.yaxis.label.set_color("white")
    ax.xaxis.label.set_color("white")
    ax.title.set_color("white")


def _save(fig, name):
    CHART_DIR.mkdir(exist_ok=True)
    path = CHART_DIR / name
    fig.savefig(path, dpi=150, bbox_inches="tight", facecolor=DARK_BG)
    plt.close(fig)
    print(f"[chart] {path.name}")


def _load(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["strategy"] = pd.Categorical(df["strategy"], categories=STRATEGY_ORDER, ordered=True)
    return df.sort_values("strategy")


# ── Build & Run ───────────────────────────────────────────────────────────────

def build_c():
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"],
                   cwd=BUILD_DIR, check=True)
    subprocess.run(["cmake", "--build", ".", "--", f"-j{4}"],
                   cwd=BUILD_DIR, check=True)
    print("[build] Done.")


def run_real(max_windows: int):
    RESULTS_DIR.mkdir(exist_ok=True)
    binary = BUILD_DIR / "or_backtest"
    if not binary.exists():
        sys.exit(f"[error] {binary} not found — build first.")
    cmd = [str(binary), str(FEED_DIR), str(OUT_REAL)]
    if max_windows:
        cmd.append(str(max_windows))
    subprocess.run(cmd, check=True)


def run_sim(n_scenarios: int):
    RESULTS_DIR.mkdir(exist_ok=True)
    binary = BUILD_DIR / "or_sim_backtest"
    if not binary.exists():
        sys.exit(f"[error] {binary} not found — build first.")
    subprocess.run([str(binary), str(FEED_DIR), str(OUT_SIM),
                    str(n_scenarios)], check=True)


# ── Real-data Charts ──────────────────────────────────────────────────────────

def chart_slippage_violin(df: pd.DataFrame):
    fig, ax = plt.subplots(figsize=(10, 6)); fig.patch.set_facecolor(DARK_BG)
    data   = [df[df["strategy"] == s]["slippage_bps"].dropna().values for s in STRATEGY_ORDER]
    colors = [PALETTE[s] for s in STRATEGY_ORDER]
    parts  = ax.violinplot(data, positions=range(len(STRATEGY_ORDER)),
                           showmedians=True, showextrema=False)
    for pc, col in zip(parts["bodies"], colors):
        pc.set_facecolor(col); pc.set_alpha(0.75)
    parts["cmedians"].set_colors("white"); parts["cmedians"].set_linewidth(2)
    ax.set_xticks(range(len(STRATEGY_ORDER)))
    ax.set_xticklabels(STRATEGY_ORDER, fontsize=11)
    ax.set_ylabel("Slippage (bps)"); ax.set_xlabel("Strategy")
    ax.set_title("Slippage Distribution — AAPL + MSFT + SPY (Real Data)", fontsize=13)
    ax.axhline(0, color=GRID_COL, linestyle="--", linewidth=0.8)
    _style(ax); fig.tight_layout(); _save(fig, "c_slippage_violin.png")


def chart_is_by_symbol(df: pd.DataFrame):
    symbols = sorted(df["symbol"].unique())
    x, w = np.arange(len(symbols)), 0.2
    fig, ax = plt.subplots(figsize=(11, 6)); fig.patch.set_facecolor(DARK_BG)
    for i, strat in enumerate(STRATEGY_ORDER):
        means = [df[(df["strategy"] == strat) & (df["symbol"] == sym)
                   ]["implementation_shortfall_bps"].mean() for sym in symbols]
        bars = ax.bar(x + i * w - w * 1.5, means, w,
                      label=strat, color=PALETTE[strat], alpha=0.85)
        for b in bars:
            h = b.get_height()
            ax.text(b.get_x() + b.get_width()/2, h + 0.005, f"{h:.2f}",
                    ha="center", va="bottom", color="white", fontsize=7)
    ax.set_xticks(x); ax.set_xticklabels(symbols, fontsize=12)
    ax.set_ylabel("Impl. Shortfall (bps)"); ax.set_xlabel("Symbol")
    ax.set_title("Implementation Shortfall by Symbol & Strategy", fontsize=13)
    ax.legend(facecolor=PANEL_BG, labelcolor="white", framealpha=0.9)
    ax.axhline(0, color=GRID_COL, linestyle="--", linewidth=0.8)
    _style(ax); fig.tight_layout(); _save(fig, "c_impl_shortfall.png")


def chart_fill_rate_heatmap(df: pd.DataFrame):
    pivot = df.pivot_table(values="fill_rate_pct", index="strategy",
                           columns="symbol", aggfunc="mean").reindex(STRATEGY_ORDER)
    fig, ax = plt.subplots(figsize=(8, 5)); fig.patch.set_facecolor(DARK_BG)
    im = ax.imshow(pivot.values, cmap="RdYlGn", vmin=0, vmax=100, aspect="auto")
    cb = fig.colorbar(im, ax=ax)
    cb.set_label("Fill Rate %", color="white")
    cb.ax.yaxis.set_tick_params(color="white")
    plt.setp(cb.ax.yaxis.get_ticklabels(), color="white")
    ax.set_xticks(range(len(pivot.columns))); ax.set_xticklabels(pivot.columns)
    ax.set_yticks(range(len(STRATEGY_ORDER))); ax.set_yticklabels(STRATEGY_ORDER)
    ax.set_title("Fill Rate % — Strategy × Symbol", fontsize=13)
    for i in range(len(STRATEGY_ORDER)):
        for j in range(len(pivot.columns)):
            v = pivot.values[i, j]
            ax.text(j, i, f"{v:.1f}%", ha="center", va="center",
                    color="black" if v > 50 else "white",
                    fontsize=10, fontweight="bold")
    _style(ax); fig.tight_layout(); _save(fig, "c_fill_rate_heatmap.png")


# ── Sim-orders Charts ─────────────────────────────────────────────────────────

def chart_slippage_by_size(df: pd.DataFrame):
    """Slippage bps vs order size, grouped by strategy."""
    df["order_qty"] = df["order_qty"].astype(float)
    sizes = sorted(df["order_qty"].unique())
    x, w = np.arange(len(sizes)), 0.18

    fig, ax = plt.subplots(figsize=(12, 6)); fig.patch.set_facecolor(DARK_BG)
    for i, strat in enumerate(STRATEGY_ORDER):
        means = [df[(df["strategy"] == strat) & (df["order_qty"] == sz)
                   ]["slippage_bps"].mean() for sz in sizes]
        ax.plot(x, means, marker="o", label=strat, color=PALETTE[strat],
                linewidth=2, markersize=7)

    ax.set_xticks(x)
    ax.set_xticklabels([f"{int(s):,}" for s in sizes], fontsize=10)
    ax.set_xlabel("Order Size (shares)"); ax.set_ylabel("Avg Slippage (bps)")
    ax.set_title("Slippage vs Order Size — Simulated Orders (All Symbols)", fontsize=13)
    ax.legend(facecolor=PANEL_BG, labelcolor="white", framealpha=0.9)
    ax.axhline(0, color=GRID_COL, linestyle="--", linewidth=0.8)
    _style(ax); fig.tight_layout(); _save(fig, "c_sim_slippage_by_size.png")


def chart_fill_rate_by_size(df: pd.DataFrame):
    """Fill rate % vs order size (market impact proxy)."""
    df["order_qty"] = df["order_qty"].astype(float)
    sizes = sorted(df["order_qty"].unique())
    x = np.arange(len(sizes))

    fig, ax = plt.subplots(figsize=(12, 6)); fig.patch.set_facecolor(DARK_BG)
    for strat in STRATEGY_ORDER:
        means = [df[(df["strategy"] == strat) & (df["order_qty"] == sz)
                   ]["fill_rate_pct"].mean() for sz in sizes]
        ax.plot(x, means, marker="s", label=strat, color=PALETTE[strat],
                linewidth=2, markersize=7, linestyle="--")

    ax.set_xticks(x)
    ax.set_xticklabels([f"{int(s):,}" for s in sizes], fontsize=10)
    ax.set_xlabel("Order Size (shares)"); ax.set_ylabel("Avg Fill Rate (%)")
    ax.set_title("Fill Rate vs Order Size — Simulated Orders", fontsize=13)
    ax.legend(facecolor=PANEL_BG, labelcolor="white", framealpha=0.9)
    ax.set_ylim(0, 110)
    _style(ax); fig.tight_layout(); _save(fig, "c_sim_fill_rate_by_size.png")


def chart_buy_vs_sell(df: pd.DataFrame):
    """Slippage bps for BUY vs SELL per strategy."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 5), sharey=True)
    fig.patch.set_facecolor(DARK_BG)
    fig.suptitle("Slippage BPS: BUY vs SELL — Simulated Orders",
                 color="white", fontsize=13)

    for ax, side in zip(axes, ["BUY", "SELL"]):
        sub = df[df["side"] == side]
        means = [sub[sub["strategy"] == s]["slippage_bps"].mean()
                 for s in STRATEGY_ORDER]
        bars = ax.bar(STRATEGY_ORDER, means,
                      color=[PALETTE[s] for s in STRATEGY_ORDER], alpha=0.85)
        for b, v in zip(bars, means):
            ax.text(b.get_x() + b.get_width()/2, v + 0.01, f"{v:.3f}",
                    ha="center", va="bottom", color="white", fontsize=9)
        ax.set_title(f"{side} Orders", fontsize=12)
        ax.set_ylabel("Avg Slippage (bps)"); ax.set_xlabel("Strategy")
        ax.axhline(0, color=GRID_COL, linestyle="--", linewidth=0.8)
        _style(ax)

    fig.tight_layout(); _save(fig, "c_sim_buy_vs_sell.png")


# ── Summary tables ────────────────────────────────────────────────────────────

def print_real_summary(df: pd.DataFrame):
    print("\n" + "━" * 74)
    print("  C REAL-DATA BACKTEST — AAPL + MSFT + SPY")
    print("━" * 74)
    print("  NOTE: 'Exch.Lat.' = simulated exchange latency (ALPHA=1ms, BETA=5ms,")
    print("        GAMMA=15ms), summed per order. NOT routing decision latency (µs).")
    print("        For routing decision latency: run ./c/build/test_latency")
    tbl = df.groupby("strategy", observed=True).agg(
        windows=("window_idx", "count"),
        slippage=("slippage_bps", "mean"),
        is_bps=("implementation_shortfall_bps", "mean"),
        fill_rate=("fill_rate_pct", "mean"),
        fees=("fees_paid", "mean"),
        exch_lat=("exchange_latency_ms", "mean"),
    ).reindex(STRATEGY_ORDER)
    print(f"\n  {'Strategy':<12} {'Windows':>8} {'Slip(bps)':>10} {'IS(bps)':>9} {'Fill%':>7} {'Fees($)':>9} {'Exch.Lat(ms)':>13}")
    print(f"  {'-'*12} {'-'*8} {'-'*10} {'-'*9} {'-'*7} {'-'*9} {'-'*13}")
    for strat, row in tbl.iterrows():
        print(f"  {strat:<12} {int(row.windows):>8,} {row.slippage:>10.3f} "
              f"{row.is_bps:>9.3f} {row.fill_rate:>6.1f}% {row.fees:>9.2f} {row.exch_lat:>13.1f}")
    print("━" * 74)


def print_sim_summary(df: pd.DataFrame):
    print("\n" + "━" * 70)
    print("  C SIMULATED-ORDERS BACKTEST — AAPL + MSFT + SPY")
    print("  (500 / 1000 / 2500 / 5000 / 10000 shares, BUY+SELL, MARKET+LIMIT)")
    print("━" * 70)
    tbl = df.groupby("strategy", observed=True).agg(
        scenarios=("row_id", "count"),
        slippage=("slippage_bps", "mean"),
        fill_rate=("fill_rate_pct", "mean"),
        fees=("fees_paid", "mean"),
    ).reindex(STRATEGY_ORDER)
    print(f"\n  {'Strategy':<12} {'Scenarios':>10} {'Slip(bps)':>10} {'Fill%':>7} {'Fees($)':>9}")
    print(f"  {'-'*12} {'-'*10} {'-'*10} {'-'*7} {'-'*9}")
    for strat, row in tbl.iterrows():
        print(f"  {strat:<12} {int(row.scenarios):>10,} {row.slippage:>10.3f} "
              f"{row.fill_rate:>6.1f}% {row.fees:>9.2f}")
    print("━" * 70)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["real", "sim", "both"], default="both")
    ap.add_argument("--max-windows", type=int, default=0,
                    help="Real-data: max windows per dataset (0=all)")
    ap.add_argument("--n-scenarios", type=int, default=2000,
                    help="Sim: scenarios per dataset (default 2000)")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--skip-run",   action="store_true")
    args = ap.parse_args()

    if not args.skip_build:
        build_c()

    CHART_DIR.mkdir(exist_ok=True)

    if args.mode in ("real", "both"):
        if not args.skip_run:
            run_real(args.max_windows)
        if OUT_REAL.exists():
            df = _load(OUT_REAL)
            print(f"[real] {len(df):,} rows loaded")
            chart_slippage_violin(df)
            chart_is_by_symbol(df)
            chart_fill_rate_heatmap(df)
            print_real_summary(df)

    if args.mode in ("sim", "both"):
        if not args.skip_run:
            run_sim(args.n_scenarios)
        if OUT_SIM.exists():
            df = _load(OUT_SIM)
            print(f"[sim] {len(df):,} rows loaded")
            chart_slippage_by_size(df)
            chart_fill_rate_by_size(df)
            chart_buy_vs_sell(df)
            print_sim_summary(df)

    print(f"\n[done] Charts → {CHART_DIR}/")


if __name__ == "__main__":
    main()
