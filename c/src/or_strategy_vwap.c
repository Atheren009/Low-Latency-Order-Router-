/* VWAP: slice_qty[i] = parent_qty * vol[i]/Σvol. Last slice absorbs remainder. */
#include "or_routing.h"
#include <math.h>
#include <string.h>

static OrError route_vwap(
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

    int    num_slices = params ? ((const VWAPParams *)params)->num_slices : 5;
    double min_qty    = params ? ((const VWAPParams *)params)->min_qty    : 1.0;

    int n = (num_slices < n_bars) ? num_slices : n_bars;
    if (n > OR_MAX_TRANCHES) n = OR_MAX_TRANCHES;

    *out_n_tranches = n;


    double total_vol = 0.0;
    for (int i = 0; i < n; i++) total_vol += bars[i].volume;

    double weights[OR_MAX_TRANCHES];
    if (total_vol <= 0.0) {
        for (int i = 0; i < n; i++) weights[i] = 1.0 / n;
    } else {
        for (int i = 0; i < n; i++) weights[i] = bars[i].volume / total_vol;
    }


    VenueId ranked[OR_VENUE_COUNT];
    int n_v = or_rank_venues(venues, parent->side, ranked);

    if (n_v == 0) {
        for (int i = 0; i < n; i++) out_n[i] = 0;
        return OR_OK;
    }

    VenueId best    = ranked[0];
    double allocated = 0.0;

    for (int i = 0; i < n; i++) {
        double slice_qty;
        if (i == n - 1) {
            slice_qty = parent->quantity - allocated;
        } else {
            slice_qty = round(parent->quantity * weights[i] * 1e6) / 1e6;
        }

        if (slice_qty < min_qty) {
            out_n[i] = 0;
            continue;
        }

        allocated += slice_qty;

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

Strategy or_strategy_vwap(int num_slices, double min_qty) {
    static VWAPParams p;
    p.num_slices = num_slices;
    p.min_qty    = min_qty;
    return (Strategy){ "VWAP(5)", route_vwap, &p };
}
