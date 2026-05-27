/*
 * or_strategy_smart.c — Smart Order Router: cross-venue depth sweep.
 *
 * Algorithm (single tranche):
 *   1. Rank venues by fee-adjusted effective price (best first).
 *   2. Allocate min(remaining, venue_liquidity) to each venue in order.
 *   3. Skip venues with < min_qty available.
 */
#include "or_routing.h"
#include <math.h>
#include <string.h>

static OrError route_smart(
    const Order   *parent,
    Exchange       venues[OR_VENUE_COUNT],
    const Bar     *bars,
    int            n_bars,
    const void    *params,
    ChildOrder     out[OR_MAX_TRANCHES][OR_MAX_CHILDREN],
    int            out_n[OR_MAX_TRANCHES],
    int           *out_n_tranches
) {
    (void)bars; (void)n_bars;

    double min_qty = params ? ((const SmartParams *)params)->min_qty : 1.0;

    VenueId ranked[OR_VENUE_COUNT];
    int n_venues = or_rank_venues(venues, parent->side, ranked);

    *out_n_tranches = 1;
    out_n[0]        = 0;

    double remaining = parent->quantity;
    int    n_children = 0;

    for (int i = 0; i < n_venues && remaining > 0.0 && n_children < OR_MAX_CHILDREN; i++) {
        VenueId vid     = ranked[i];
        double liquidity = or_exchange_liquidity(&venues[vid], parent->side);

        if (liquidity < min_qty) continue;

        double alloc = fmin(remaining, liquidity);
        /* Round to 6 decimal places (mirrors Python's round(x, 6)) */
        alloc = round(alloc * 1e6) / 1e6;

        ChildOrder *child = &out[0][n_children++];
        memset(child, 0, sizeof(ChildOrder));
        child->parent_id   = parent->id;
        child->venue       = vid;
        child->side        = parent->side;
        child->quantity    = alloc;
        child->limit_price = 0.0;

        remaining = round((remaining - alloc) * 1e6) / 1e6;
    }

    out_n[0] = n_children;
    return OR_OK;
}

Strategy or_strategy_smart(double min_qty) {
    /* SmartParams is small; embed in a static — safe for single-threaded use.
     * For multi-threaded, allocate on heap and free after backtest.          */
    static SmartParams p;
    p.min_qty = min_qty;
    return (Strategy){ "Smart", route_smart, &p };
}
