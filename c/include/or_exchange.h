/*
 * or_exchange.h — Simulated exchange wrapping an OrderBook.
 *
 * Three venues (ALPHA, BETA, GAMMA) are pre-configured in or_types.h.
 * Each Exchange holds one OrderBook and a reference price for slippage calc.
 */
#ifndef OR_EXCHANGE_H
#define OR_EXCHANGE_H

#include "or_types.h"
#include "or_order_book.h"

/* ── Exchange ───────────────────────────────────────────────────────── */
typedef struct {
    VenueId   id;
    OrderBook book;
    double    ref_price;   /* VWAP of last seeded bar — slippage anchor */
} Exchange;

/* ── API ────────────────────────────────────────────────────────────── */

/* Initialise an exchange for the given venue. */
void or_exchange_init(Exchange *ex, VenueId id);

/* Reset book + ref_price without re-seeding. */
void or_exchange_reset(Exchange *ex);

/*
 * Seed from a bar: reset book and plant 3 ask + 3 bid limit orders.
 *
 * Ask levels: vwap + spread_bias + i*0.05  (i = 0,1,2)
 * Bid levels: vwap - spread_bias - i*0.05  (i = 0,1,2)
 * Qty per level: max(10.0, bar.volume / 30)
 */
void or_exchange_seed(Exchange *ex, const Bar *bar);

/*
 * Submit a ChildOrder as a MARKET aggressor against this exchange's book.
 * Returns a populated FillResult.
 */
FillResult or_exchange_submit(Exchange *ex, const ChildOrder *child);

/* Best taker-side quote: BUY → ask, SELL → bid. 0.0 if empty. */
double or_exchange_quote(const Exchange *ex, OrderSide side);

/* Fee-adjusted quote for cross-venue ranking. 0.0 if no quote. */
double or_exchange_effective_price(const Exchange *ex, OrderSide side);

/* Total resting liquidity on the taker side. */
double or_exchange_liquidity(const Exchange *ex, OrderSide side);

#endif /* OR_EXCHANGE_H */
