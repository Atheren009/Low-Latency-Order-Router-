/*
 * or_routing.h — Strategy interface and all four strategy declarations.
 *
 * Strategies are plain functions matching RouteFn; a Strategy struct bundles
 * the name, function pointer, and per-strategy parameter block.
 *
 * Single-tranche strategies (BestPrice, Smart):
 *   out_tranches[0][0..n-1] = children for the one tranche
 *   *out_n_tranches = 1
 *
 * Multi-tranche strategies (TWAP, VWAP):
 *   out_tranches[t][0] = child for tranche t
 *   *out_n_tranches = number of tranches (up to OR_MAX_TRANCHES)
 */
#ifndef OR_ROUTING_H
#define OR_ROUTING_H

#include "or_types.h"
#include "or_exchange.h"

/* ── Strategy function signature ────────────────────────────────────── */
/*
 * @param parent         Parent order to route.
 * @param venues         Array of OR_VENUE_COUNT exchanges (pre-seeded).
 * @param bars           Price bars for time-sliced strategies; may be NULL.
 * @param n_bars         Number of bars in bars[].
 * @param params         Strategy-specific parameter struct; may be NULL.
 * @param out_tranches   Output: 2D array [tranche][child_index].
 * @param out_n_per_tranche  Output: children count per tranche.
 * @param out_n_tranches Output: total number of tranches produced.
 * @return OR_OK or an error code.
 */
typedef OrError (*RouteFn)(
    const Order   *parent,
    Exchange       venues[OR_VENUE_COUNT],
    const Bar     *bars,
    int            n_bars,
    const void    *params,
    ChildOrder     out_tranches[OR_MAX_TRANCHES][OR_MAX_CHILDREN],
    int            out_n_per_tranche[OR_MAX_TRANCHES],
    int           *out_n_tranches
);

/* ── Strategy descriptor ────────────────────────────────────────────── */
typedef struct {
    const char *name;
    RouteFn     route;
    void       *params;   /* heap-allocated param struct or NULL        */
} Strategy;

/* ── Strategy parameter structs ─────────────────────────────────────── */
typedef struct { double min_qty;   } SmartParams;
typedef struct { int num_slices;   } TWAPParams;
typedef struct { int num_slices;
                 double min_qty;   } VWAPParams;
/* BestPrice has no params */

/* ── Strategy constructors ───────────────────────────────────────────── */

/* BestPrice: routes entire order to cheapest fee-adjusted venue.       */
Strategy or_strategy_best_price(void);

/* Smart: fee-adjusted cross-venue depth sweep (single tranche).        */
Strategy or_strategy_smart(double min_qty);

/* TWAP: equal slices across num_slices bars.                           */
Strategy or_strategy_twap(int num_slices);

/* VWAP: volume-weighted slices across num_slices bars.                 */
Strategy or_strategy_vwap(int num_slices, double min_qty);

/* ── Shared helper: rank venues by effective price ──────────────────── */
/*
 * Fills out_venue_ids[0..count-1] with venue indices sorted best-first:
 *   BUY  → ascending effective ask (cheaper to buy)
 *   SELL → descending effective bid (better to sell)
 * Returns number of venues that had a quote.
 */
int or_rank_venues(
    Exchange   venues[OR_VENUE_COUNT],
    OrderSide  side,
    VenueId    out_venue_ids[OR_VENUE_COUNT]
);

#endif /* OR_ROUTING_H */
