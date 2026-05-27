/*
 * or_sim_orders.c — Deterministic synthetic order generator.
 *
 * Uses a simple xorshift64 PRNG seeded by the caller. All scenarios are
 * reproducible: same seed + same bars → same sequence.
 *
 * Size distribution: uniform across OR_SIM_N_SIZES (500..10000).
 * Side distribution: strict alternation BUY → SELL → BUY → …
 * Type distribution: 80 % MARKET, 20 % LIMIT (at vwap ± 0.05 %).
 */
#include "or_sim_orders.h"
#include <stdio.h>
#include <string.h>

/* xorshift64 — fast, period 2^64-1, zero-dependency PRNG */
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

        /* Pick a random bar */
        int bar_idx = (int)(xorshift64(&rng) % (uint64_t)n_bars);
        sc->bar       = bars[bar_idx];
        sc->ref_price = bars[bar_idx].vwap;

        /* Size class: uniform over 5 sizes */
        int size_class     = (int)(xorshift64(&rng) % OR_SIM_N_SIZES);
        sc->size_class     = size_class;
        double qty         = OR_SIM_SIZES[size_class];

        /* Side: strict alternation */
        OrderSide side = (i % 2 == 0) ? SIDE_BUY : SIDE_SELL;

        /* Type: 80 % MARKET, 20 % LIMIT */
        int is_limit = (xorshift64(&rng) % 10) < 2;  /* 2/10 = 20 % */

        double limit_price = 0.0;
        if (is_limit) {
            /* LIMIT priced at vwap ± 0.05 % on the aggressive side */
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

        /* Label for CSV output */
        snprintf(sc->label, sizeof(sc->label), "%s_%s_%.0f",
                 (side == SIDE_BUY) ? "BUY" : "SELL",
                 is_limit            ? "LIMIT" : "MARKET",
                 qty);
    }
}
