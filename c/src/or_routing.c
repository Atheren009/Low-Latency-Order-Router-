/*
 * or_routing.c — Shared venue ranking helper used by all strategies.
 */
#include "or_routing.h"

/*
 * Sort 3 venues by effective price.
 * BUY:  ascending effective ask  (cheapest first)
 * SELL: descending effective bid (highest first)
 *
 * Insertion sort — always exactly 3 elements, O(1) in practice.
 */
int or_rank_venues(
    Exchange   venues[OR_VENUE_COUNT],
    OrderSide  side,
    VenueId    out[OR_VENUE_COUNT]
) {
    double ep[OR_VENUE_COUNT];
    int    valid[OR_VENUE_COUNT];
    int    n = 0;

    for (int i = 0; i < OR_VENUE_COUNT; i++) {
        double e = or_exchange_effective_price(&venues[i], side);
        if (e > 0.0) {
            ep[n]    = e;
            valid[n] = i;
            n++;
        }
    }

    /* Insertion sort (ascending for BUY, descending for SELL) */
    for (int i = 1; i < n; i++) {
        double ke = ep[i];
        int    kv = valid[i];
        int    j  = i - 1;
        if (side == SIDE_BUY) {
            while (j >= 0 && ep[j] > ke) { ep[j+1] = ep[j]; valid[j+1] = valid[j]; j--; }
        } else {
            while (j >= 0 && ep[j] < ke) { ep[j+1] = ep[j]; valid[j+1] = valid[j]; j--; }
        }
        ep[j+1]    = ke;
        valid[j+1] = kv;
    }

    for (int i = 0; i < n; i++) out[i] = (VenueId)valid[i];
    return n;
}
