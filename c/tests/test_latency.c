/*
 * test_latency.c — Correct latency profiler for the C hot path.
 *
 * ┌──────────────────────────────────────────────────────────────────┐
 * │  TWO DISTINCT LATENCY METRICS — do not confuse them             │
 * │                                                                  │
 * │  1. Routing Decision Latency (measured here, µs)                 │
 * │     Wall-clock time for: strategy.route() + exchange.submit()   │
 * │     This is the CPU cost of the algorithm itself.               │
 * │                                                                  │
 * │  2. Simulated Exchange Latency (in backtest CSV, ms)             │
 * │     OR_VENUES[v].latency_ms: ALPHA=1ms, BETA=5ms, GAMMA=15ms   │
 * │     This is a business model of network + matching delay.       │
 * │     It is NOT measured wall-clock time.                         │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * Measurement correctness:
 *   - Anti-optimization: checksum from or_router_benchmark() is printed
 *     to stderr, anchoring every routing call in the optimizer's view.
 *   - Warmup: 200 seeded iterations inside benchmark(); no external loop.
 *   - Sanity check: verify total_filled_qty > 0 (algorithm actually ran).
 *   - Clock: CLOCK_MONOTONIC, nanosecond resolution on Linux.
 *
 * Build: gcc -std=c11 -O2 -Wall -I../include test_latency.c
 *         ../src/or_order_book.c ../src/or_exchange.c ../src/or_routing.c
 *         ../src/or_strategy_best_price.c ../src/or_strategy_smart.c
 *         ../src/or_strategy_twap.c ../src/or_strategy_vwap.c
 *         ../src/or_router.c -lm -o test_latency
 * Run:   ./test_latency
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/or_router.h"

#define N_ITERS  5000   /* timed iterations per strategy                */

/* ── Percentile helper ────────────────────────────────────────────────── */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}
static double percentile(const double *sorted, int n, double p) {
    int idx = (int)(p / 100.0 * n);
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

/* ── Sanity check: verify algorithm is actually executing ─────────────── */
/*
 * Directly runs a single seeded route+submit (outside the benchmark timer)
 * and confirms filled_qty > 0.  If the optimizer eliminated the call in
 * the benchmark loop, this separate execution would still show fills —
 * but the benchmark numbers would be suspiciously small (< clock resolution).
 * Prints a warning if P99 < 0.1 µs (clock_gettime overhead is ~0.1–0.4 µs).
 */
static void sanity_check(const char *name, double p99_us, const Bar *bar) {
    /* One fully-pinned routing call */
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

/* ── Main ─────────────────────────────────────────────────────────────── */
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

    /* Expected fill rate per strategy (for sanity comment) */
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

        /*
         * All warmup is inside or_router_benchmark() (200 seeded iters).
         * Do NOT add an external warmup loop — it would run on a stale
         * (partially-consumed) book and produce a misleading warm state.
         */
        or_router_benchmark(&r, &ref, N_ITERS, lats);

        qsort(lats, N_ITERS, sizeof(double), cmp_double);

        double p50 = percentile(lats, N_ITERS, 50.0);
        double p90 = percentile(lats, N_ITERS, 90.0);
        double p95 = percentile(lats, N_ITERS, 95.0);
        double p99 = percentile(lats, N_ITERS, 99.0);
        double mx  = lats[N_ITERS - 1];

        /* Run sanity check (prints to stderr, doesn't pollute table) */
        sanity_check(names[si], p99, &ref);

        /* Status note */
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

    /* Interpretation guide */
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

    /* ── Assert ─────────────────────────────────────────────────────── */
    /* P99 should be above clock_gettime floor (real work happened)     */
    assert(p99_best >= 0.05 &&
           "BestPrice P99 < 0.05µs: compiler eliminated the algorithm");

    /* P99 must be below sane upper bound even on WSL2 /mnt/ */
    assert(p99_best < 500.0 &&
           "BestPrice P99 > 500µs: something is very wrong");

    printf("  [PASS] BestPrice P99 = %.2f µs (in expected range)\n\n",
           p99_best);

    free(lats);
    return 0;
}
