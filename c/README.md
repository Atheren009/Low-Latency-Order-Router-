# Order Router — C Engine

Low-latency order routing engine written in **C11**, targeting sub-5µs P99
on the hot path (route + match), down from ~195µs in Python.

---

## Architecture

```
CSV files (Python price_feed.py)
      │
      ▼
PriceFeed → Bar[]
      │
      ▼ or_exchange_seed()          ← one Exchange per venue
   Exchange[]  (ALPHA / BETA / GAMMA)
      │
      ├── OrderBook  ─── sorted price-level arrays + circular FIFO
      │
      ▼ Strategy.route()
   RoutingStrategy   ─── BestPrice / Smart / TWAP / VWAP
      │  (function pointer table, zero vtable overhead)
      │
      ▼ or_exchange_submit()
   FillResult[]  ──► aggregated RouteResult
      │
      ▼
   BacktestEngine (C)  ──► results/c_backtest_results.csv
      │
      ▼
   Python Analytics   ──► matplotlib charts
```

---

## File Map

```
c/
├── include/
│   ├── or_types.h          All enums, structs, venue config, ID generator
│   ├── or_order_book.h     Price-time priority book API
│   ├── or_exchange.h       Exchange seeding + submit API
│   ├── or_routing.h        Strategy interface + all strategy declarations
│   ├── or_router.h         Orchestration + benchmark API
│   ├── or_backtest.h       Real-data backtest (3 datasets)
│   ├── or_sim_orders.h     Synthetic order generator
│   └── or_csv.h            CSV bar loader
├── src/
│   ├── or_order_book.c     Matching engine
│   ├── or_exchange.c       Venue seeding + fill calculation
│   ├── or_routing.c        Venue ranking helper (insertion sort × 3)
│   ├── or_strategy_best_price.c
│   ├── or_strategy_smart.c
│   ├── or_strategy_twap.c
│   ├── or_strategy_vwap.c
│   ├── or_router.c         Tranche orchestration + benchmark
│   ├── or_csv.c            fgets + strtok_r CSV parser
│   ├── or_backtest.c       Sliding-window backtest engine
│   ├── or_sim_orders.c     xorshift64 synthetic order generator
│   ├── main_backtest.c     CLI entry: real data backtest
│   └── main_sim_backtest.c CLI entry: simulated orders backtest
└── tests/
    ├── test_order_book.c   20 tests
    ├── test_exchange.c     10 tests
    ├── test_strategies.c   15 tests
    ├── test_latency.c      P50/P90/P95/P99 profiler
    └── test_sim_orders.c   5 tests
```

---

## Build (WSL2 / Ubuntu)

```bash
# Install dependencies (once)
sudo apt-get install -y cmake gcc

# One-command build
bash scripts/build.sh release

# Build + run all 50 tests
bash scripts/build.sh release test
```

Or manually:

```bash
cd c
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

---

## Run Backtests

### Real-data backtest (AAPL + MSFT + SPY, sliding window)

```bash
# C binary directly:
./c/build/or_backtest "Price Feed" results/c_backtest_results.csv

# With Python analytics (builds, runs, generates charts):
python scripts/run_c_backtest.py

# Limit to 1000 windows per dataset for quick test:
python scripts/run_c_backtest.py --max-windows 1000
```

### Simulated-orders backtest (BUY/SELL, 5 size classes, MARKET/LIMIT)

```bash
# C binary directly:
./c/build/or_sim_backtest "Price Feed" results/c_sim_results.csv 2000

# Skip build if already built:
python scripts/run_c_backtest.py --skip-build
```

### Latency profiler

```bash
# C binary (raw output):
./c/build/test_latency

# Python wrapper:
python scripts/c_latency_profile.py
```

---

## Key Design Decisions

| Python | C replacement | Reason |
|---|---|---|
| `SortedDict` (B-tree) | Fixed sorted array, binary search | Cache-friendly, no heap |
| `uuid.uuid4()` string IDs | `uint64_t` atomic counter | No `malloc`, no string ops |
| `datetime` objects | `int64_t` row index | No object overhead |
| `dataclass` | Packed C struct | Stack-allocated |
| `dict` venue lookup | `enum VenueId` + static array | Zero hash cost |
| Abstract base class | Function pointer table | Same dispatch, no vtable |
| `deque` per price level | Circular buffer (inline array) | No heap, cache-local |

---

## Expected Latency vs Python

| Metric | Python P99 | C P99 target | Improvement |
|---|---|---|---|
| BestPrice | 195 µs | < 5 µs | ~40× |
| Smart | ~335 µs | < 10 µs | ~35× |
| TWAP | ~500 µs | < 15 µs | ~35× |
| Full seeding (cold) | 260 µs | < 10 µs | ~26× |

---

## What Stays in Python

| File | Purpose |
|---|---|
| `order_router/price_feed.py` | CSV loading for Python tests |
| `order_router/backtest.py` | Original Python backtest (kept for comparison) |
| `order_router/comparator.py` | matplotlib/pandas analytics |
| `scripts/run_c_backtest.py` | Calls C binary, generates charts |
| `scripts/c_latency_profile.py` | Runs C latency profiler |
