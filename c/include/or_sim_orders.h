/* Synthetic order generator for stress-testing */
#ifndef OR_SIM_ORDERS_H
#define OR_SIM_ORDERS_H

#include "or_types.h"

/* Sizes: 500–10k, alternating BUY/SELL, 80% MARKET / 20% LIMIT */
#define OR_SIM_N_SIZES  5
static const double OR_SIM_SIZES[OR_SIM_N_SIZES] = {
    500.0, 1000.0, 2500.0, 5000.0, 10000.0
};

typedef struct {
    Order   parent;
    Bar     bar;
    double  ref_price;     /* vwap of the bar                           */
    int     size_class;    /* index into OR_SIM_SIZES (0–4)             */
    char    label[32];     /* e.g. "BUY_MARKET_5000"                    */
} SimScenario;

/* Generate n_scenarios deterministic synthetic scenarios from bars[] */
void or_sim_generate(
    const Bar   *bars,
    int          n_bars,
    uint64_t     seed,
    SimScenario *out,
    int          n_scenarios
);

#endif /* OR_SIM_ORDERS_H */
