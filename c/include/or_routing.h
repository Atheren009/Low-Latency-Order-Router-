/* Strategy interface and routing declarations */
#ifndef OR_ROUTING_H
#define OR_ROUTING_H

#include "or_types.h"
#include "or_exchange.h"

/*
 * Single-tranche (BestPrice, Smart): out_tranches[0][0..n-1], *out_n_tranches = 1
 * Multi-tranche  (TWAP, VWAP):      out_tranches[t][0],      *out_n_tranches = T
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

typedef struct {
    const char *name;
    RouteFn     route;
    void       *params;
} Strategy;

typedef struct { double min_qty;   } SmartParams;
typedef struct { int num_slices;   } TWAPParams;
typedef struct { int num_slices;
                 double min_qty;   } VWAPParams;

Strategy or_strategy_best_price(void);
Strategy or_strategy_smart(double min_qty);
Strategy or_strategy_twap(int num_slices);
Strategy or_strategy_vwap(int num_slices, double min_qty);

/*
 * Rank venues best-first:
 *   BUY  → ascending effective ask
 *   SELL → descending effective bid
 * Returns count of venues that had a quote.
 */
int or_rank_venues(
    Exchange   venues[OR_VENUE_COUNT],
    OrderSide  side,
    VenueId    out_venue_ids[OR_VENUE_COUNT]
);

#endif /* OR_ROUTING_H */
