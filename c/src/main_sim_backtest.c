/*
 * main_sim_backtest.c — Simulated-orders backtest entry point.
 *
 * Combines real AAPL + MSFT + SPY bars with synthetic orders of
 * varying sizes (500 / 1000 / 2500 / 5000 / 10000 shares) and sides
 * (BUY / SELL), producing a richer strategy comparison.
 *
 * Usage:
 *   ./build/or_sim_backtest <price_feed_dir> <output_csv> [n_scenarios_per_dataset]
 *
 * Example:
 *   ./build/or_sim_backtest "Price Feed" results/c_sim_results.csv 2000
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/or_types.h"
#include "../include/or_csv.h"
#include "../include/or_exchange.h"
#include "../include/or_routing.h"
#include "../include/or_sim_orders.h"

#define MAX_SCENARIOS 10000
#define N_STRATEGIES  4

static const char *SYMBOLS[3]  = { "AAPL", "MSFT", "SPY" };
static const char *PATHS_FMT[] = {
    "%s/AAPL_1min_2024-2026.csv",
    "%s/MSFT_1min_2024-2026.csv",
    "%s/SPY_1min_2024-2026.csv",
};

/* Inline mini-router (avoids double-seeding from Router struct) */
static void run_scenario(
    const SimScenario *sc,
    Strategy          *strategy,
    Exchange           venues[OR_VENUE_COUNT],
    FILE              *out,
    int                row_id,
    const char        *symbol
) {
    /* Seed venues from scenario bar */
    for (int v = 0; v < OR_VENUE_COUNT; v++)
        or_exchange_seed(&venues[v], &sc->bar);

    Bar bars5[5]; for (int i = 0; i < 5; i++) bars5[i] = sc->bar;

    ChildOrder tranche_out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int        out_n[OR_MAX_TRANCHES];
    int        n_tranches = 0;

    /* Make a copy — route() may not modify parent but we play safe */
    Order parent = sc->parent;
    parent.id = or_next_id();  /* fresh ID for this run */

    strategy->route(&parent, venues, bars5, 5, strategy->params,
                    tranche_out, out_n, &n_tranches);

    double total_filled   = 0.0;
    double total_notional = 0.0;
    double total_fees     = 0.0;
    int    total_lat      = 0;

    for (int t = 0; t < n_tranches; t++) {
        if (t > 0)
            for (int v = 0; v < OR_VENUE_COUNT; v++)
                or_exchange_seed(&venues[v], &sc->bar);  /* re-seed between tranches */

        for (int c = 0; c < out_n[t]; c++) {
            ChildOrder *child = &tranche_out[t][c];
            if (child->quantity <= 0.0) continue;
            FillResult fr = or_exchange_submit(&venues[child->venue], child);
            if (fr.filled_qty > 0.0) {
                total_notional += fr.avg_price * fr.filled_qty;
                total_filled   += fr.filled_qty;
                total_fees     += fr.fees_paid;
                total_lat      += fr.latency_ms;
            }
        }
    }

    double avg_price    = (total_filled > 0.0) ? total_notional / total_filled : 0.0;
    double slippage_bps = 0.0;
    double is_bps       = 0.0;
    double fill_rate    = (sc->parent.quantity > 0.0)
                         ? total_filled / sc->parent.quantity * 100.0 : 0.0;

    if (avg_price > 0.0 && sc->ref_price > 0.0) {
        slippage_bps = (avg_price - sc->ref_price) / sc->ref_price * 10000.0;
        is_bps       = slippage_bps;  /* for market orders IS == slippage */
    }

    fprintf(out,
        "%d,%s,%s,%s,%.0f,%s,%.6f,%.6f,%.6f,%.4f,%.2f,%.4f,%d\n",
        row_id, symbol, strategy->name, sc->label,
        sc->parent.quantity,
        (sc->parent.side == SIDE_BUY) ? "BUY" : "SELL",
        total_filled, avg_price, total_fees,
        slippage_bps, fill_rate, is_bps, total_lat
    );
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s <price_feed_dir> <output_csv> [n_scenarios]\n",
            argv[0]);
        return 1;
    }
    const char *feed_dir   = argv[1];
    const char *out_path   = argv[2];
    int n_scenarios        = (argc >= 4) ? atoi(argv[3]) : 2000;
    if (n_scenarios > MAX_SCENARIOS) n_scenarios = MAX_SCENARIOS;

    /* Load datasets */
    static Dataset datasets[3];
    for (int d = 0; d < 3; d++) {
        char path[600];
        snprintf(path, sizeof(path), PATHS_FMT[d], feed_dir);
        int n = or_csv_load(path, SYMBOLS[d], &datasets[d]);
        if (n < 0) { fprintf(stderr, "[error] Cannot load %s\n", path); return 1; }
        fprintf(stderr, "[sim] Loaded %d bars for %s\n", n, SYMBOLS[d]);
    }

    /* Build strategies */
    Strategy strategies[N_STRATEGIES] = {
        or_strategy_best_price(),
        or_strategy_smart(1.0),
        or_strategy_twap(5),
        or_strategy_vwap(5, 1.0),
    };

    /* Open output */
    FILE *out = fopen(out_path, "w");
    if (!out) { fprintf(stderr, "[error] Cannot open %s\n", out_path); return 1; }
    fprintf(out,
        "row_id,symbol,strategy,order_label,order_qty,side,"
        "filled_qty,avg_price,fees_paid,slippage_bps,"
        /* exchange_latency_ms = simulated venue latency, NOT routing decision µs */
        "fill_rate_pct,impl_shortfall_bps,exchange_latency_ms\n"
    );

    static SimScenario scenarios[MAX_SCENARIOS];
    Exchange venues[OR_VENUE_COUNT];
    for (int v = 0; v < OR_VENUE_COUNT; v++)
        or_exchange_init(&venues[v], (VenueId)v);

    int row_id = 0;
    uint64_t seeds[3] = { 0xAAAABBBBCCCC1234ULL,
                           0x1234567890ABCDEFULL,
                           0xFEDCBA9876543210ULL };

    for (int d = 0; d < 3; d++) {
        or_sim_generate(datasets[d].bars, datasets[d].n_bars,
                        seeds[d], scenarios, n_scenarios);

        fprintf(stderr, "[sim] Running %d scenarios × %d strategies for %s...\n",
                n_scenarios, N_STRATEGIES, SYMBOLS[d]);

        for (int i = 0; i < n_scenarios; i++) {
            for (int si = 0; si < N_STRATEGIES; si++) {
                run_scenario(&scenarios[i], &strategies[si],
                             venues, out, row_id++, SYMBOLS[d]);
            }
            if (i % 500 == 0)
                fprintf(stderr, "  [%s] scenario %d/%d\n", SYMBOLS[d], i, n_scenarios);
        }
    }

    fclose(out);
    fprintf(stderr, "[sim] Done. %d rows → %s\n", row_id, out_path);
    return 0;
}
