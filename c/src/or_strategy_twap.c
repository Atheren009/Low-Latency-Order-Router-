/* TWAP: split order into equal-sized time slices. */
#include "or_routing.h"
#include <math.h>
#include <string.h>

static OrError route_twap(
    const Order   *parent,
    Exchange       venues[OR_VENUE_COUNT],
    const Bar     *bars,
    int            n_bars,
    const void    *params,
    ChildOrder     out[OR_MAX_TRANCHES][OR_MAX_CHILDREN],
    int            out_n[OR_MAX_TRANCHES],
    int           *out_n_tranches
) {
    if (bars == NULL || n_bars == 0) return OR_ERR_NO_BARS;

    int num_slices = params ? ((const TWAPParams *)params)->num_slices : 5;
    int n = (num_slices < n_bars) ? num_slices : n_bars;
    if (n > OR_MAX_TRANCHES) n = OR_MAX_TRANCHES;

    *out_n_tranches = n;

    VenueId ranked[OR_VENUE_COUNT];
    int n_v = or_rank_venues(venues, parent->side, ranked);

    if (n_v == 0) {
        for (int i = 0; i < n; i++) out_n[i] = 0;
        return OR_OK;
    }

    VenueId best     = ranked[0];
    double slice_qty = round((parent->quantity / n) * 1e6) / 1e6;

    for (int i = 0; i < n; i++) {
        ChildOrder *child = &out[i][0];
        memset(child, 0, sizeof(ChildOrder));
        child->parent_id   = parent->id;
        child->venue       = best;
        child->side        = parent->side;
        child->quantity    = slice_qty;
        child->limit_price = 0.0;
        out_n[i]           = 1;
    }
    return OR_OK;
}

Strategy or_strategy_twap(int num_slices) {
    static TWAPParams p;
    p.num_slices = num_slices;
    return (Strategy){ "TWAP(5)", route_twap, &p };
}
