/*
 * or_backtest.c — Multi-dataset, multi-strategy backtesting engine.
 *
 * Two latency concepts in this file (do NOT confuse):
 *   exchange_latency_ms : simulated venue latency (ALPHA=1ms, BETA=5ms, GAMMA=15ms)
 *                         Written to the output CSV. Business metric, not wall-clock.
 *   routing decision µs : NOT measured here — see test_latency for that.
 *
 * Sliding window model:
 *   For each dataset: step through bars[0..N-1] in windows of OR_WINDOW_SIZE.
 *   bars[0]        → seed venues
 *   bars[1..W-1]   → available for TWAP/VWAP slicing
 *   parent order   → 5000 shares BUY MARKET
 *
 * All four strategies are run against the same seeded state per window.
 */
#include "or_backtest.h"
#include "or_router.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Implementation shortfall = (avg_fill_price - ref_price) / ref_price * 10000 */
static double impl_shortfall(double avg_price, double ref_price) {
    if (ref_price <= 0.0 || avg_price <= 0.0) return 0.0;
    return (avg_price - ref_price) / ref_price * 10000.0;
}

/*
 * Snapshot the exchange state then run ONE strategy against it.
 * We snapshot by re-seeding from bar[0] before each strategy call so
 * every strategy sees the same fresh liquidity state.
 */
static BacktestRow run_one_window(
    const Bar     *window,        /* window[0..OR_WINDOW_SIZE-1] */
    const char    *symbol,
    int            window_idx,
    Strategy      *strategy,
    Exchange       venues[OR_VENUE_COUNT]   /* caller-allocated, pre-inited */
) {
    /* Re-seed all venues from the first bar of the window */
    for (int v = 0; v < OR_VENUE_COUNT; v++)
        or_exchange_seed(&venues[v], &window[0]);

    double ref_price = window[0].vwap;

    /* Build parent order */
    Order parent = {
        .id        = or_next_id(),
        .side      = SIDE_BUY,
        .type      = TYPE_MARKET,
        .status    = STATUS_OPEN,
        .quantity  = OR_ORDER_QTY,
        .price     = 0.0,
        .filled_qty= 0.0,
    };

    /* Inline mini-router (no Router struct — avoid double-seeding) */
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int        out_n[OR_MAX_TRANCHES];
    int        n_tranches = 0;

    strategy->route(&parent, venues, window + 1, OR_WINDOW_SIZE - 1,
                    strategy->params, out, out_n, &n_tranches);

    double total_filled   = 0.0;
    double total_notional = 0.0;
    double total_fees     = 0.0;
    int    total_lat      = 0;

    for (int t = 0; t < n_tranches; t++) {
        /* Re-seed from next window bar between tranches */
        if (t > 0 && (t < OR_WINDOW_SIZE - 1))
            for (int v = 0; v < OR_VENUE_COUNT; v++)
                or_exchange_seed(&venues[v], &window[t + 1]);

        for (int c = 0; c < out_n[t]; c++) {
            ChildOrder *child = &out[t][c];
            if (child->quantity <= 0.0) continue;

            FillResult fr = or_exchange_submit(&venues[child->venue], child);
            if (fr.filled_qty > 0.0) {
                total_notional += fr.avg_price * fr.filled_qty;
                total_filled   += fr.filled_qty;
                total_fees     += fr.fees_paid;
                /* fr.latency_ms = OR_VENUES[v].latency_ms: ALPHA=1, BETA=5, GAMMA=15 */
                total_lat      += fr.latency_ms;
            }
        }
    }

    BacktestRow row;
    row.window_idx      = window_idx;
    strncpy(row.symbol, symbol, OR_MAX_SYMBOL - 1);
    row.symbol[OR_MAX_SYMBOL - 1] = '\0';
    strncpy(row.strategy, strategy->name, sizeof(row.strategy) - 1);
    row.strategy[sizeof(row.strategy) - 1] = '\0';
    row.filled_qty      = total_filled;
    row.avg_price       = (total_filled > 0.0) ? total_notional / total_filled : 0.0;
    row.fees_paid       = total_fees;
    row.slippage_bps    = (total_filled > 0.0 && ref_price > 0.0)
                          ? (row.avg_price - ref_price) / ref_price * 10000.0 : 0.0;
    row.fill_rate_pct   = (OR_ORDER_QTY > 0.0) ? total_filled / OR_ORDER_QTY * 100.0 : 0.0;
    row.impl_shortfall_bps = impl_shortfall(row.avg_price, ref_price);
    row.exchange_latency_ms = total_lat;   /* simulated venue latency, NOT wall-clock */
    return row;
}

int or_backtest_run(const BacktestConfig *config) {
    /* Load all datasets */
    static Dataset datasets[OR_N_DATASETS];
    for (int d = 0; d < OR_N_DATASETS; d++) {
        int n = or_csv_load(config->dataset_paths[d],
                            config->symbol_filters[d],
                            &datasets[d]);
        if (n < 0) {
            fprintf(stderr, "[backtest] Failed to load %s\n",
                    config->dataset_paths[d]);
            return -1;
        }
        fprintf(stderr, "[backtest] Loaded %d bars from %s (%s)\n",
                n, config->dataset_paths[d], config->symbol_filters[d]);
    }

    /* Build strategies */
    Strategy strategies[OR_N_STRATEGIES] = {
        or_strategy_best_price(),
        or_strategy_smart(1.0),
        or_strategy_twap(config->n_slices),
        or_strategy_vwap(config->n_slices, 1.0),
    };

    /* Open output CSV */
    FILE *out = fopen(config->output_csv, "w");
    if (!out) {
        fprintf(stderr, "[backtest] Cannot open output: %s\n", config->output_csv);
        return -1;
    }
    fprintf(out,
        "window_idx,symbol,strategy,filled_qty,avg_price,fees_paid,"
        /* exchange_latency_ms = simulated venue latency (ms), NOT routing decision latency */
        "slippage_bps,fill_rate_pct,implementation_shortfall_bps,exchange_latency_ms\n");

    /* Per-dataset venue set (reused across windows) */
    Exchange venues[OR_N_DATASETS][OR_VENUE_COUNT];
    for (int d = 0; d < OR_N_DATASETS; d++)
        for (int v = 0; v < OR_VENUE_COUNT; v++)
            or_exchange_init(&venues[d][v], (VenueId)v);

    int total_rows = 0;
    int global_window = 0;

    /* Slide window over each dataset */
    for (int d = 0; d < OR_N_DATASETS; d++) {
        const Dataset *ds = &datasets[d];
        int n_windows = ds->n_bars - OR_WINDOW_SIZE + 1;
        if (config->max_windows > 0 && n_windows > config->max_windows)
            n_windows = config->max_windows;

        fprintf(stderr, "[backtest] Running %d windows for %s...\n",
                n_windows, ds->symbol);

        for (int w = 0; w < n_windows; w++) {
            const Bar *window = &ds->bars[w];

            for (int s = 0; s < OR_N_STRATEGIES; s++) {
                BacktestRow row = run_one_window(
                    window, ds->symbol,
                    global_window, &strategies[s],
                    venues[d]
                );
                fprintf(out,
                    "%d,%s,%s,%.6f,%.6f,%.6f,%.4f,%.2f,%.4f,%d\n",
                    row.window_idx, row.symbol, row.strategy,
                    row.filled_qty, row.avg_price, row.fees_paid,
                    row.slippage_bps, row.fill_rate_pct,
                    row.impl_shortfall_bps, row.exchange_latency_ms
                );
                total_rows++;
            }
            global_window++;

            if (w % 500 == 0)
                fprintf(stderr, "  [%s] window %d/%d\n",
                        ds->symbol, w, n_windows);
        }
    }

    fclose(out);
    fprintf(stderr, "[backtest] Done. %d rows written to %s\n",
            total_rows, config->output_csv);
    return total_rows;
}
