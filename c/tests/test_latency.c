/*
 * Latency profiler for the C routing hot path.
 *
 * Two distinct metrics — don't confuse them:
 *   1. Routing Decision Latency (measured here, µs) — CPU cost of route()+submit()
 *   2. Simulated Exchange Latency (backtest CSV, ms) — modeled network delay, NOT wall-clock
 *
 * Anti-DCE: checksum from or_router_benchmark() is printed to stderr so
 * the optimizer can't eliminate routing calls. Warmup is internal (200 iters).
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/or_router.h"

#define N_ITERS  5000   /* timed iterations per strategy                */

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}
static double percentile(const double *sorted, int n, double p) {
    int idx = (int)(p / 100.0 * n);
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

/* Runs one route+submit outside the timer to confirm fills actually happen */
static void sanity_check(const char *name, double p99_us, const Bar *bar) {

    Router r2;
    Strategy s = or_strategy_best_price();
    or_router_init(&r2, s);
    or_router_seed(&r2, bar);

    Bar bars5[5]; for (int i = 0; i < 5; i++) bars5[i] = *bar;
    Order p = { .id = or_next_id(), .side = SIDE_BUY, .type = TYPE_MARKET,
                .status = STATUS_OPEN, .quantity = 100.0 };
    RouteResult res;
    or_router_submit(&r2, &p, bars5, 5, bar->vwap, &res);

    if (res.total_filled_qty <= 0.0) {
        fprintf(stderr,
            "[WARN] %s: sanity fill = 0! Book may be empty after seed.\n", name);
    }

    if (p99_us < 0.1) {
        fprintf(stderr,
            "[WARN] %s: P99 = %.3f µs < 0.1 µs (clock_gettime floor).\n"
            "       Dead-code elimination likely — check compiler flags.\n"
            "       Expected minimum: ~0.5 µs for BestPrice on hot L1 cache.\n",
            name, p99_us);
    }
}


int main(void) {
    Bar ref = {
        .row_index = 0, .open = 189.5, .high = 190.2, .low = 189.3,
        .close = 189.95, .volume = 8500.0, .trade_count = 120, .vwap = 189.82
    };

    Strategy strats[4] = {
        or_strategy_best_price(),
        or_strategy_smart(1.0),
        or_strategy_twap(5),
        or_strategy_vwap(5, 1.0),
    };
    const char *names[4] = { "BestPrice", "Smart", "TWAP(5)", "VWAP(5)" };


    const char *expect[4] = {
        "100% fill, 1 venue",
        "100% fill, 1-3 venues",
        "100% fill, 5 tranches",
        "100% fill, 5 tranches vol-weighted",
    };

    double *lats = malloc(sizeof(double) * N_ITERS);
    assert(lats && "malloc failed");

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║         C Routing Decision Latency  (N=%5d, -O2)          ║\n", N_ITERS);
    printf("║  Metric: wall-clock time for route() + exchange.submit()    ║\n");
    printf("║  NOT exchange latency (that is in the backtest CSV, ms)     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("  %-12s  %8s  %8s  %8s  %8s  %8s\n",
           "Strategy", "P50(µs)", "P90(µs)", "P95(µs)", "P99(µs)", "Max(µs)");
    printf("  %-12s  %8s  %8s  %8s  %8s  %8s\n",
           "──────────", "───────", "───────", "───────", "───────", "───────");

    double p99_best = -1.0;

    for (int si = 0; si < 4; si++) {
        Router r;
        or_router_init(&r, strats[si]);

        /* warmup is inside or_router_benchmark() — don't add an external loop,
         * it'd run on a stale (consumed) book and give misleading numbers */
        or_router_benchmark(&r, &ref, N_ITERS, lats);

        qsort(lats, N_ITERS, sizeof(double), cmp_double);

        double p50 = percentile(lats, N_ITERS, 50.0);
        double p90 = percentile(lats, N_ITERS, 90.0);
        double p95 = percentile(lats, N_ITERS, 95.0);
        double p99 = percentile(lats, N_ITERS, 99.0);
        double mx  = lats[N_ITERS - 1];


        sanity_check(names[si], p99, &ref);


        const char *status;
        if (p99 < 0.1)       status = "⚠ DCE suspected";
        else if (p99 < 5.0)  status = "✓ excellent";
        else if (p99 < 20.0) status = "✓ good";
        else if (p99 < 100.0)status = "✓ ok";
        else                  status = "CHECK";

        printf("  %-12s  %8.2f  %8.2f  %8.2f  %8.2f  %8.2f  %s\n",
               names[si], p50, p90, p95, p99, mx, status);
        printf("  %-12s  %s\n", "", expect[si]);
        printf("\n");

        if (si == 0) p99_best = p99;
    }


    printf("\n");
    printf("  ── Interpretation ─────────────────────────────────────────\n");
    printf("  P50: median latency (most iterations look like this)\n");
    printf("  P99: 99th percentile (worst-case except 1%% outliers)\n");
    printf("  Max: single worst sample (often OS preemption spike)\n");
    printf("\n");
    printf("  Minimum physically possible:\n");
    printf("    clock_gettime() overhead on Linux:   ~0.1 – 0.4 µs\n");
    printf("    BestPrice theoretical minimum:        ~0.5 – 2.0 µs\n");
    printf("    TWAP(5) (5 × reseed + match):         ~5  – 30   µs\n");
    printf("\n");
    printf("  If P99 < 0.2 µs for any strategy → dead-code elimination.\n");
    printf("  Recheck: compile WITHOUT -O2, confirm numbers are larger.\n");
    printf("\n");

    /* P99 must be above clock_gettime floor (real work happened) */
    assert(p99_best >= 0.05 &&
           "BestPrice P99 < 0.05µs: compiler eliminated the algorithm");


    assert(p99_best < 500.0 &&
           "BestPrice P99 > 500µs: something is very wrong");

    printf("  [PASS] BestPrice P99 = %.2f µs (in expected range)\n\n",
           p99_best);

    free(lats);
    return 0;
}
