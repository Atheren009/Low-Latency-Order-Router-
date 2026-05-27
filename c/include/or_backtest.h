/*
 * or_backtest.h — Multi-dataset, multi-strategy backtesting engine.
 *
 * Runs a sliding window over each dataset with all four strategies,
 * then writes aggregated results to a CSV for Python analytics.
 *
 * Window model: each "window" is a group of OR_WINDOW_SIZE consecutive bars.
 * For each window:
 *   bar[0]          → seed all venues
 *   bars[1..N-1]    → available for TWAP/VWAP slicing
 *   parent order    → 5000 shares BUY MARKET
 *
 * Three datasets are processed simultaneously (AAPL, MSFT, SPY).
 */
#ifndef OR_BACKTEST_H
#define OR_BACKTEST_H

#include "or_types.h"
#include "or_csv.h"
#include "or_routing.h"

#define OR_WINDOW_SIZE   6    /* bars per window: 1 seed + 5 slices     */
#define OR_N_STRATEGIES  4    /* BestPrice, Smart, TWAP(5), VWAP(5)     */
#define OR_N_DATASETS    3    /* AAPL, MSFT, SPY                        */
#define OR_ORDER_QTY  5000.0  /* parent order size (shares)             */

/* ── Per-window result row ──────────────────────────────────────────── */
typedef struct {
    int      window_idx;
    char     symbol[OR_MAX_SYMBOL];
    char     strategy[32];
    double   filled_qty;
    double   avg_price;
    double   fees_paid;
    double   slippage_bps;
    double   fill_rate_pct;
    double   impl_shortfall_bps;  /* (avg_price - ref_price) / ref_price * 10000 */
    /*
     * exchange_latency_ms: simulated venue network + matching latency.
     * NOT wall-clock time. Source: OR_VENUES[venue].latency_ms
     *   ALPHA = 1 ms, BETA = 5 ms, GAMMA = 15 ms.
     * Summed across all fills in the window.
     * For routing decision latency (µs), run test_latency.
     */
    int      exchange_latency_ms;
} BacktestRow;

/* ── Backtest configuration ─────────────────────────────────────────── */
typedef struct {
    const char *dataset_paths[OR_N_DATASETS];
    const char *symbol_filters[OR_N_DATASETS];
    const char *output_csv;       /* path for results CSV                */
    int         max_windows;      /* 0 = use all available bars          */
    int         n_slices;         /* TWAP/VWAP slice count (default 5)   */
} BacktestConfig;

/* ── API ────────────────────────────────────────────────────────────── */

/*
 * Run the full backtest across all datasets and strategies.
 * Results are written to config->output_csv as CSV.
 * Returns total number of rows written, or -1 on error.
 */
int or_backtest_run(const BacktestConfig *config);

#endif /* OR_BACKTEST_H */
