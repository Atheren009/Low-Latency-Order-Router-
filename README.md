# Order Router

I built an order router that finds the cheapest way to execute trades across multiple exchanges. Tested on 14,985 real AAPL/MSFT/SPY market windows. VWAP strategy saves 0.55 bps per trade over naive routing — on a desk moving $500M/day, that's **$6.5M/year** left on the table if you route dumb.

Hot path is C. Analytics are Python. No dependencies on the critical path.

---

## The Journey

I started this to answer one question: **if markets are fragmented across multiple exchanges, how much money do you lose by always routing to the same one?**

Built a naive router first (always pick the cheapest venue). Watched it fill only 49% of orders and leave 2.09 bps of slippage on every trade. Then built VWAP — splits orders across time and venues, weighted by volume. Fill rate jumped to 67%, slippage dropped to 1.54 bps.

The difference? 0.55 bps. Sounds tiny. On real volume it's not.

---

## What the Data Says

Backtested 14,985 order windows across AAPL, MSFT, and SPY (1-min bars, 2024–2026). Each window: 5,000-share BUY MARKET order routed through 3 simulated venues.

**BestPrice** — always pick the cheapest venue
- 49.4% fill rate · 2.09 bps slippage
- Fast but leaves half the order unfilled

**Smart** — sweep venues by depth, greedy allocation
- 61.3% fill rate · 2.18 bps slippage
- More fills, but actually *worse* slippage (see Gotchas)

**TWAP(5)** — equal slices over 5 intervals
- 64.9% fill rate · 1.55 bps slippage
- Patient execution pays off

**VWAP(5)** — volume-weighted slices over 5 intervals
- 66.7% fill rate · 1.54 bps slippage
- Best overall: 35% more fills AND 26% less slippage than BestPrice

**The tradeoff**: VWAP needs ~20µs more routing time than BestPrice. On a $500M/day desk, that latency cost is paid back in roughly 1 second of trading revenue.

---

### Slippage Distribution

![Slippage violin](results/c_charts/c_slippage_violin.png)

VWAP and TWAP have tighter distributions with fewer blowups. BestPrice has a long tail — when it's bad, it's really bad.

---

### Implementation Shortfall by Ticker

![Impl shortfall](results/c_charts/c_impl_shortfall.png)

Smart routing consistently beats BestPrice across all three symbols. The effect is strongest on SPY (most liquid) where venue competition matters most.

---

### Fill Rate Heatmap

![Fill rate heatmap](results/c_charts/c_fill_rate_heatmap.png)

BestPrice tops out at ~49% because it only hits one venue. VWAP sweeps all three and gets 67%.

---

### Market Impact vs Order Size (Simulated)

![Slippage by size](results/c_charts/c_sim_slippage_by_size.png)

Ran synthetic orders from 500 to 10,000 shares. BestPrice slippage grows fast with size. Smart routing keeps it flatter by splitting across venues.

---

### Fill Rate Degradation

![Fill rate by size](results/c_charts/c_sim_fill_rate_by_size.png)

Large orders choke on single-venue depth. Multi-venue strategies hold up.

---

### BUY vs SELL Asymmetry

![Buy vs sell](results/c_charts/c_sim_buy_vs_sell.png)

Consistent behavior on both sides — the routing logic is side-agnostic, which is what you want.

---

## What Went Wrong (So You Don't)

- **Smart strategy underperformed on slippage.** It fills more orders by sweeping 3 venues, but the extra venue fees (BETA: 0.15%, GAMMA: 0.25%) eat the gains. Intelligence ≠ better in all cases. VWAP wins because it's patient, not because it's clever.

- **Cold-start latency was brutal.** Seeding 3 order books took ~260µs in Python — 75% of total routing time. In production you'd amortize this across thousands of orders. The C version seeds in <10µs.

- **I measured latency wrong.** First benchmark showed BestPrice at 0.32µs P99. Turns out GCC optimized away the entire routing call because I never read the result. Fixed with a `volatile` checksum sink. Real number: ~2–5µs. ([see the fix](c/src/or_router.c))

- **Two different "latency" numbers confused everything.** The backtest CSV has `exchange_latency_ms` (simulated network delay: 1/5/15ms per venue). The profiler measures `routing_decision_µs` (CPU time for the algorithm). Completely different metrics, same word. Renamed everything to make it obvious.

---

## Architecture

```
CSV bars → or_exchange_seed() → 3 venues (cold path, not timed)
                                    │
                              Strategy.route() → hot path, < 5µs P99
                                    │
                              or_exchange_submit() → price-time matching
                                    │
                              RouteResult → CSV → Python charts
```

The C engine does zero heap allocation on the hot path. Everything is stack-allocated or in static buffers.

| What Python had | What C uses | Why |
|---|---|---|
| `SortedDict` (B-tree) | Fixed array, 8 levels max | Fits in L1 cache |
| `uuid4()` strings | `uint64_t` atomic counter | No malloc |
| `deque` per level | Circular buffer, inline | No pointer chasing |
| `dict` venue lookup | `enum` + static array | Zero hash cost |
| ABC + vtable | Function pointer | Same dispatch, less indirection |

---

## Project Structure

```
c/                      ← the engine (hot path)
  include/              8 headers
  src/                  13 source files
  tests/                50 tests across 5 files
  CMakeLists.txt

order_router/           ← Python reference implementation
  routing/              BestPrice, Smart, TWAP, VWAP
  order_book.py         matching engine
  exchange.py           venue simulation
  backtest.py           sliding-window backtester

scripts/
  build.sh              one-command WSL2 build
  run_c_backtest.py     build → run → 7 charts
  profile_latency.py    Python latency profiler

results/c_charts/       committed output charts
```

---

## Run It Yourself

```bash
# build + run all 50 C tests
bash scripts/build.sh release test

# backtest on real data (needs Price Feed CSVs)
./c/build/or_backtest "Price Feed" results/c_backtest_results.csv

# simulated orders (varying size/side)
./c/build/or_sim_backtest "Price Feed" results/c_sim_results.csv 2000

# latency profiler
./c/build/test_latency

# generate charts
python scripts/run_c_backtest.py --skip-build --skip-run
```

Needs `cmake` and `gcc` on Linux/WSL2. Python side needs `pandas` and `matplotlib`.

---

## Notes on the Data

I use real 1-minute OHLCV data from yfinance and simulate 3 exchanges with realistic latency profiles (ALPHA: 1ms/0.10% fee, BETA: 5ms/0.15%, GAMMA: 15ms/0.25%). Real deployment would swap in actual exchange feeds (NASDAQ ITCH, NYSE TAQ). The routing logic doesn't change — just the data source.

---

**Why I built this**: I wanted to understand how execution quality separates good trading desks from mediocre ones. Turns out the answer is 0.55 basis points — invisible on any single trade, worth millions at scale. The math always wins.
