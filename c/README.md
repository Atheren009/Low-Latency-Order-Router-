# C Engine

The hot path. Everything here runs in < 5µs P99 for BestPrice routing.

## Build

```bash
bash scripts/build.sh release test
```

Or manually:

```bash
cd c && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

## Binaries

- `or_backtest` — real-data sliding-window backtest (AAPL + MSFT + SPY)
- `or_sim_backtest` — synthetic orders (5 sizes × BUY/SELL × MARKET/LIMIT)
- `test_latency` — P50/P90/P95/P99 routing decision profiler

## Files

Headers define the API. Sources implement it. Tests prove it works.

```
include/or_types.h       enums, structs, venue config
include/or_order_book.h  price-time priority book
include/or_exchange.h    venue seeding + matching
include/or_routing.h     strategy interface (function pointers)
include/or_router.h      orchestration + benchmark harness
include/or_backtest.h    3-dataset backtester
include/or_sim_orders.h  synthetic order generator
include/or_csv.h         bar loader

src/                     13 files, ~600 lines of actual logic
tests/                   50 tests, 5 files
```

## Latency note

Two different metrics — don't confuse them:

1. **Routing decision (µs)**: wall-clock `route() + submit()`. Run `test_latency`.
2. **Exchange latency (ms)**: simulated network delay per venue (1/5/15 ms). Shows up in backtest CSV as `exchange_latency_ms`.
