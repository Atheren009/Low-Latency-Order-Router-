/* Multi-dataset, multi-strategy backtester */
#ifndef OR_BACKTEST_H
#define OR_BACKTEST_H

#include "or_types.h"
#include "or_csv.h"
#include "or_routing.h"

#define OR_WINDOW_SIZE   6    /* 1 seed bar + 5 slices                   */
#define OR_N_STRATEGIES  4
#define OR_N_DATASETS    3    /* AAPL, MSFT, SPY                         */
#define OR_ORDER_QTY  5000.0

typedef struct {
    int      window_idx;
    char     symbol[OR_MAX_SYMBOL];
    char     strategy[32];
    double   filled_qty;
    double   avg_price;
    double   fees_paid;
    double   slippage_bps;
    double   fill_rate_pct;
    double   impl_shortfall_bps;  /* (avg_price - ref_price) / ref_price * 1e4 */
    /*
     * Simulated venue latency, NOT wall-clock. Sourced from OR_VENUES[].latency_ms.
     * For routing decision latency (µs), run test_latency instead.
     */
    int      exchange_latency_ms;
} BacktestRow;

typedef struct {
    const char *dataset_paths[OR_N_DATASETS];
    const char *symbol_filters[OR_N_DATASETS];
    const char *output_csv;
    int         max_windows;      /* 0 = all available bars              */
    int         n_slices;         /* TWAP/VWAP slice count (default 5)   */
} BacktestConfig;

/* Returns total rows written to config->output_csv, or -1 on error */
int or_backtest_run(const BacktestConfig *config);

#endif /* OR_BACKTEST_H */
