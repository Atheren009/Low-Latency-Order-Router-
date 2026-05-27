/*
 * or_router.c — Orchestrates strategy → exchange pipeline.
 *
 * Hot path (measured):
 *   strategy.route() → exchange.submit() per child
 *
 * Cold path (not measured):
 *   or_router_seed() → exchange.seed_from_bar() × 3
 */
#include "or_router.h"
#include <stdio.h>
#include <string.h>
#include <time.h>


/* Nanosecond wall clock (CLOCK_MONOTONIC) */
static inline int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void or_router_init(Router *r, Strategy strategy) {
    r->strategy = strategy;
    for (int v = 0; v < OR_VENUE_COUNT; v++)
        or_exchange_init(&r->venues[v], (VenueId)v);
}

void or_router_seed(Router *r, const Bar *bar) {
    for (int v = 0; v < OR_VENUE_COUNT; v++)
        or_exchange_seed(&r->venues[v], bar);
}

OrError or_router_submit(
    Router       *r,
    const Order  *parent,
    const Bar    *bars,
    int           n_bars,
    double        ref_price,
    RouteResult  *result
) {
    /* Plan tranches */
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int        out_n[OR_MAX_TRANCHES];
    int        n_tranches = 0;

    OrError err = r->strategy.route(
        parent, r->venues, bars, n_bars, r->strategy.params,
        out, out_n, &n_tranches
    );
    if (err != OR_OK) return err;

    /* Aggregate across all tranches */
    double total_filled  = 0.0;
    double total_notional= 0.0;
    double total_fees    = 0.0;
    int    total_lat     = 0;
    int    n_fills       = 0;

    for (int t = 0; t < n_tranches; t++) {
        /* Re-seed from next bar before each tranche (simulates time passing).
         * The first tranche uses the already-seeded state.                   */
        if (t > 0 && t < n_bars)
            or_router_seed(r, &bars[t]);

        for (int c = 0; c < out_n[t]; c++) {
            ChildOrder *child = &out[t][c];
            if (child->quantity <= 0.0) continue;

            FillResult fr = or_exchange_submit(&r->venues[child->venue], child);
            child->fill     = fr;
            child->has_fill = true;

            if (fr.filled_qty > 0.0) {
                total_notional += fr.avg_price * fr.filled_qty;
                total_filled   += fr.filled_qty;
                total_fees     += fr.fees_paid;
                total_lat      += fr.latency_ms;
                n_fills++;
            }
        }
    }

    /* Build result */
    result->total_filled_qty = total_filled;
    result->total_fees_paid  = total_fees;
    result->total_latency_ms = total_lat;
    result->n_trades         = n_fills;

    if (total_filled > 0.0) {
        result->avg_fill_price = total_notional / total_filled;
        double ref = (ref_price > 0.0) ? ref_price : result->avg_fill_price;
        result->slippage_bps =
            (result->avg_fill_price - ref) / ref * 10000.0;
    } else {
        result->avg_fill_price = 0.0;
        result->slippage_bps   = 0.0;
    }

    result->fill_rate_pct = (parent->quantity > 0.0)
        ? (total_filled / parent->quantity) * 100.0 : 0.0;

    return OR_OK;
}

/*
 * Anti-dead-code-elimination sink.
 *
 * Problem: with -O2, GCC's alias analysis proves that `res` (stack variable)
 * cannot be observed after or_router_submit() returns, so it eliminates the
 * entire function call body.  The timer then measures two clock_gettime() calls
 * (~200–400 ns), not the routing algorithm.
 *
 * Fix: accumulate res.total_filled_qty into a volatile double.  The `volatile`
 * qualifier tells the compiler: "this write has an observable side-effect you
 * must not eliminate."  We also print the checksum after the loop, so the
 * entire chain — route → fill → accumulate → print — is proven to the compiler
 * to be visible.
 */
void or_router_benchmark(
    Router       *r,
    const Bar    *bar,
    int           n_iters,
    double       *out_us
) {
    Bar bars5[5];
    for (int i = 0; i < 5; i++) bars5[i] = *bar;

    /* ── Warmup: 200 iterations, not timed ─────────────────────────── */
    volatile double warmup_sink = 0.0;
    for (int i = 0; i < 200; i++) {
        or_router_seed(r, bar);
        Order p = { .id = or_next_id(), .side = SIDE_BUY, .type = TYPE_MARKET,
                    .status = STATUS_OPEN, .quantity = 5000.0 };
        RouteResult res;
        or_router_submit(r, &p, bars5, 5, bar->vwap, &res);
        warmup_sink += res.total_filled_qty;  /* prevent elimination */
    }
    (void)warmup_sink;

    /* ── Timed iterations ────────────────────────────────────────────
     * Seed is OUTSIDE the timer (cold path).
     * route() + submit() is INSIDE the timer (hot path).
     * Checksum accumulates across all iterations so the optimizer
     * cannot hoist or remove the submit() call.                      */
    volatile double checksum = 0.0;

    for (int i = 0; i < n_iters; i++) {
        or_router_seed(r, bar);   /* reseed — not timed */

        Order p = { .id = or_next_id(), .side = SIDE_BUY, .type = TYPE_MARKET,
                    .status = STATUS_OPEN, .quantity = 5000.0 };
        RouteResult res;

        int64_t t0 = now_ns();
        or_router_submit(r, &p, bars5, 5, bar->vwap, &res);
        int64_t t1 = now_ns();

        /* Accumulate AFTER stopping the timer — no bias on measurement */
        checksum += res.total_filled_qty;

        out_us[i] = (double)(t1 - t0) / 1000.0;
    }

    /*
     * Print checksum so the compiler treats every write as visible.
     * Format: one line to stderr so it doesn't pollute stdout tables.
     * Expected: total_filled_qty = 5000.0 per iteration (fully filled).
     */
    fprintf(stderr, "[benchmark] checksum=%.0f (expected %.0f)\n",
            (double)checksum, (double)(n_iters * 5000.0));
}
