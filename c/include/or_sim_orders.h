/*
 * or_sim_orders.h — Synthetic order generator for stress-testing.
 *
 * Generates deterministic sequences of Orders with varying:
 *   - Sizes  : 500, 1000, 2500, 5000, 10000 shares
 *   - Sides  : BUY and SELL (alternating)
 *   - Types  : MARKET (80 %) and LIMIT (20 %, priced at vwap ± 0.05 %)
 *
 * Used by the backtest engine alongside real OHLCV bars to validate
 * strategy behaviour across a full order-size distribution.
 */
#ifndef OR_SIM_ORDERS_H
#define OR_SIM_ORDERS_H

#include "or_types.h"

/* Canonical order sizes exercised by the simulator. */
#define OR_SIM_N_SIZES  5
static const double OR_SIM_SIZES[OR_SIM_N_SIZES] = {
    500.0, 1000.0, 2500.0, 5000.0, 10000.0
};

/* ── SimScenario: one synthetic parent order + its bar context ────── */
typedef struct {
    Order   parent;
    Bar     bar;           /* reference bar used to seed venues         */
    double  ref_price;     /* vwap of the bar                           */
    int     size_class;    /* index into OR_SIM_SIZES (0–4)             */
    char    label[32];     /* human-readable: e.g. "BUY_MARKET_5000"   */
} SimScenario;

/*
 * Generate n_scenarios synthetic scenarios from a pool of bars.
 *
 * @param bars        Source bars (from any dataset).
 * @param n_bars      Number of bars available.
 * @param seed        RNG seed for reproducibility.
 * @param out         Output array (caller-allocated, size >= n_scenarios).
 * @param n_scenarios Number of scenarios to generate.
 */
void or_sim_generate(
    const Bar   *bars,
    int          n_bars,
    uint64_t     seed,
    SimScenario *out,
    int          n_scenarios
);

#endif /* OR_SIM_ORDERS_H */
