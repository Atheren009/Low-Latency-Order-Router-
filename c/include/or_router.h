/* Order router: strategy → exchange → aggregated result */
#ifndef OR_ROUTER_H
#define OR_ROUTER_H

#include "or_types.h"
#include "or_exchange.h"
#include "or_routing.h"

typedef struct {
    Exchange  venues[OR_VENUE_COUNT];
    Strategy  strategy;
} Router;

void or_router_init(Router *r, Strategy strategy);

/* Seed all venues from a bar — this is the cold path */
void or_router_seed(Router *r, const Bar *bar);

/*
 * bars[] must have >= tranches the strategy produces (for TWAP/VWAP).
 * ref_price: pass bar.vwap for slippage calc.
 */
OrError or_router_submit(
    Router       *r,
    const Order  *parent,
    const Bar    *bars,
    int           n_bars,
    double        ref_price,
    RouteResult  *result
);

/* Warm-loop latency profiler, writes µs samples to out_us[] */
void or_router_benchmark(
    Router       *r,
    const Bar    *bar,
    int           n_iters,
    double       *out_us
);

#endif /* OR_ROUTER_H */
