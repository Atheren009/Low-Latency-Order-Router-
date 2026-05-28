/* Deterministic synthetic order generator (xorshift64).
 * Sizes: uniform 500..10k. Sides: alternating. Type: 80% MARKET / 20% LIMIT. */
#include "or_sim_orders.h"
#include <stdio.h>
#include <string.h>


static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

void or_sim_generate(
    const Bar   *bars,
    int          n_bars,
    uint64_t     seed,
    SimScenario *out,
    int          n_scenarios
) {
    if (n_bars <= 0 || n_scenarios <= 0) return;

    uint64_t rng = (seed == 0) ? 0xDEADBEEFCAFEBABEULL : seed;

    for (int i = 0; i < n_scenarios; i++) {
        SimScenario *sc = &out[i];


        int bar_idx = (int)(xorshift64(&rng) % (uint64_t)n_bars);
        sc->bar       = bars[bar_idx];
        sc->ref_price = bars[bar_idx].vwap;


        int size_class     = (int)(xorshift64(&rng) % OR_SIM_N_SIZES);
        sc->size_class     = size_class;
        double qty         = OR_SIM_SIZES[size_class];


        OrderSide side = (i % 2 == 0) ? SIDE_BUY : SIDE_SELL;


        int is_limit = (xorshift64(&rng) % 10) < 2;

        double limit_price = 0.0;
        if (is_limit) {

            double bias = sc->ref_price * 0.0005;
            limit_price = (side == SIDE_BUY)
                          ? sc->ref_price + bias
                          : sc->ref_price - bias;
        }

        sc->parent = (Order){
            .id         = or_next_id(),
            .side       = side,
            .type       = is_limit ? TYPE_LIMIT : TYPE_MARKET,
            .status     = STATUS_OPEN,
            .quantity   = qty,
            .price      = limit_price,
            .filled_qty = 0.0,
        };


        snprintf(sc->label, sizeof(sc->label), "%s_%s_%.0f",
                 (side == SIDE_BUY) ? "BUY" : "SELL",
                 is_limit            ? "LIMIT" : "MARKET",
                 qty);
    }
}
