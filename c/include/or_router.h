/*
 * or_router.h — OrderRouter: orchestrates strategy → exchange → result.
 *
 * The router:
 *   1. Calls strategy.route() to get tranches of ChildOrders.
 *   2. For each tranche, submits every ChildOrder to its target exchange.
 *   3. Re-seeds venues from the next bar between tranches (simulates time).
 *   4. Aggregates all FillResults into a single RouteResult.
 */
#ifndef OR_ROUTER_H
#define OR_ROUTER_H

#include "or_types.h"
#include "or_exchange.h"
#include "or_routing.h"

/* ── Router context (reused across orders — warm path) ─────────────── */
typedef struct {
    Exchange  venues[OR_VENUE_COUNT];   /* pre-seeded and kept alive    */
    Strategy  strategy;
} Router;

/* ── API ────────────────────────────────────────────────────────────── */

/* Initialise router with a strategy. Venues are NOT seeded yet.        */
void or_router_init(Router *r, Strategy strategy);

/*
 * Seed all venues from a bar (call once per new market-data tick).
 * This is the "cold path" — measured separately from route+submit.
 */
void or_router_seed(Router *r, const Bar *bar);

/*
 * Submit a parent order:
 *   - Calls strategy.route() to plan tranches.
 *   - For each tranche, submits children and re-seeds from bars[tranche_i].
 *   - Fills *result with aggregated execution metrics.
 *
 * bars[] must have at least as many elements as the number of tranches
 * the strategy will produce (required for TWAP/VWAP; ignored by others).
 *
 * @param ref_price   Reference price for slippage calculation.
 *                    Pass bar.vwap of the current bar.
 */
OrError or_router_submit(
    Router       *r,
    const Order  *parent,
    const Bar    *bars,
    int           n_bars,
    double        ref_price,
    RouteResult  *result
);

/* Built-in latency profiler: runs n_iters warm submits and returns
 * latency samples in out_us[] (caller-allocated, size >= n_iters).     */
void or_router_benchmark(
    Router       *r,
    const Bar    *bar,
    int           n_iters,
    double       *out_us
);

#endif /* OR_ROUTER_H */
