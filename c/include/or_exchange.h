/* Simulated exchange wrapping an OrderBook */
#ifndef OR_EXCHANGE_H
#define OR_EXCHANGE_H

#include "or_types.h"
#include "or_order_book.h"

typedef struct {
    VenueId   id;
    OrderBook book;
    double    ref_price;   /* VWAP of last seeded bar — slippage anchor  */
} Exchange;

void or_exchange_init(Exchange *ex, VenueId id);
void or_exchange_reset(Exchange *ex);

/*
 * Seed from a bar: reset book, plant 3 ask + 3 bid levels.
 * Asks: vwap + spread_bias + i*0.05, Bids: vwap - spread_bias - i*0.05
 * Qty per level: max(10.0, bar.volume / 30)
 */
void or_exchange_seed(Exchange *ex, const Bar *bar);

/* Submit a ChildOrder as MARKET aggressor, returns FillResult. */
FillResult or_exchange_submit(Exchange *ex, const ChildOrder *child);

/* BUY → ask, SELL → bid. 0.0 if empty. */
double or_exchange_quote(const Exchange *ex, OrderSide side);

/* Fee-adjusted quote for cross-venue ranking */
double or_exchange_effective_price(const Exchange *ex, OrderSide side);

double or_exchange_liquidity(const Exchange *ex, OrderSide side);

#endif /* OR_EXCHANGE_H */
