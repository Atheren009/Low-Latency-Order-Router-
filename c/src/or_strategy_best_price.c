/* BestPrice: send everything to the cheapest venue. */
#include "or_routing.h"
#include <string.h>

static OrError route_best_price(
    const Order   *parent,
    Exchange       venues[OR_VENUE_COUNT],
    const Bar     *bars,
    int            n_bars,
    const void    *params,
    ChildOrder     out[OR_MAX_TRANCHES][OR_MAX_CHILDREN],
    int            out_n[OR_MAX_TRANCHES],
    int           *out_n_tranches
) {
    (void)bars; (void)n_bars; (void)params;

    VenueId ranked[OR_VENUE_COUNT];
    int n = or_rank_venues(venues, parent->side, ranked);

    *out_n_tranches = 1;
    out_n[0]        = 0;

    if (n == 0) return OR_OK;

    ChildOrder *child = &out[0][0];
    memset(child, 0, sizeof(ChildOrder));
    child->parent_id   = parent->id;
    child->venue       = ranked[0];
    child->side        = parent->side;
    child->quantity    = parent->quantity;
    child->limit_price = 0.0;
    out_n[0] = 1;
    return OR_OK;
}

Strategy or_strategy_best_price(void) {
    return (Strategy){ "BestPrice", route_best_price, NULL };
}
