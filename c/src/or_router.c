/* Strategy → exchange pipeline orchestrator. */
#include "or_router.h"
#include <stdio.h>
#include <string.h>
#include <time.h>



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

    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int        out_n[OR_MAX_TRANCHES];
    int        n_tranches = 0;

    OrError err = r->strategy.route(
        parent, r->venues, bars, n_bars, r->strategy.params,
        out, out_n, &n_tranches
    );
    if (err != OR_OK) return err;


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

/* Anti-DCE: without volatile, -O2 proves res is unobservable after
 * or_router_submit() returns and eliminates the entire call body.
 * You'd measure two clock_gettime() calls (~300ns), not the router. */
void or_router_benchmark(
    Router       *r,
    const Bar    *bar,
    int           n_iters,
    double       *out_us
) {
    Bar bars5[5];
    for (int i = 0; i < 5; i++) bars5[i] = *bar;

    volatile double warmup_sink = 0.0;
    for (int i = 0; i < 200; i++) {
        or_router_seed(r, bar);
        Order p = { .id = or_next_id(), .side = SIDE_BUY, .type = TYPE_MARKET,
                    .status = STATUS_OPEN, .quantity = 5000.0 };
        RouteResult res;
        or_router_submit(r, &p, bars5, 5, bar->vwap, &res);
        warmup_sink += res.total_filled_qty;
    }
    (void)warmup_sink;

    volatile double checksum = 0.0;

    for (int i = 0; i < n_iters; i++) {
        or_router_seed(r, bar);

        Order p = { .id = or_next_id(), .side = SIDE_BUY, .type = TYPE_MARKET,
                    .status = STATUS_OPEN, .quantity = 5000.0 };
        RouteResult res;

        int64_t t0 = now_ns();
        or_router_submit(r, &p, bars5, 5, bar->vwap, &res);
        int64_t t1 = now_ns();


        checksum += res.total_filled_qty;

        out_us[i] = (double)(t1 - t0) / 1000.0;
    }

    /* print so compiler can't prove checksum is dead */
    fprintf(stderr, "[benchmark] checksum=%.0f (expected %.0f)\n",
            (double)checksum, (double)(n_iters * 5000.0));
}
