/*
 * or_exchange.c — Simulated exchange: seeding + order submission.
 *
 * Seeding plants 3 ask + 3 bid limit orders per venue. Each level uses
 * a deterministic ID so we never call or_next_id() during seeding
 * (avoiding atomic overhead on the cold path).
 *
 * Deterministic seed IDs:
 *   ask level i:  0xA000_0000_0000_0000 | (venue_id << 4) | i
 *   bid level i:  0xB000_0000_0000_0000 | (venue_id << 4) | i
 */
#include "or_exchange.h"
#include <math.h>
#include <string.h>

/* Round to 4 decimal places (mirrors Python's round(x, 4)). */
static inline double r4(double x) {
    return round(x * 10000.0) / 10000.0;
}

void or_exchange_init(Exchange *ex, VenueId id) {
    ex->id        = id;
    ex->ref_price = 0.0;
    or_book_init(&ex->book);
}

void or_exchange_reset(Exchange *ex) {
    ex->ref_price = 0.0;
    or_book_init(&ex->book);
}

void or_exchange_seed(Exchange *ex, const Bar *bar) {
    or_book_init(&ex->book);   /* wipe old liquidity */
    ex->ref_price = bar->vwap;

    const VenueConfig *cfg = &OR_VENUES[ex->id];
    double level_qty = fmax(10.0, bar->volume / 30.0);
    double ask_base  = r4(bar->vwap + cfg->spread_bias);
    double bid_base  = r4(bar->vwap - cfg->spread_bias);

    for (int i = 0; i < 3; i++) {
        /* Ask level */
        Order ask = {
            .id        = 0xA000000000000000ULL | ((uint64_t)ex->id << 4) | i,
            .side      = SIDE_SELL,
            .type      = TYPE_LIMIT,
            .status    = STATUS_OPEN,
            .quantity  = level_qty,
            .price     = r4(ask_base + i * 0.05),
            .filled_qty= 0.0,
        };
        or_book_rest(&ex->book, &ask);

        /* Bid level */
        Order bid = {
            .id        = 0xB000000000000000ULL | ((uint64_t)ex->id << 4) | i,
            .side      = SIDE_BUY,
            .type      = TYPE_LIMIT,
            .status    = STATUS_OPEN,
            .quantity  = level_qty,
            .price     = r4(bid_base - i * 0.05),
            .filled_qty= 0.0,
        };
        or_book_rest(&ex->book, &bid);
    }
}

FillResult or_exchange_submit(Exchange *ex, const ChildOrder *child) {
    FillResult result = {
        .venue      = ex->id,
        .order_id   = 0,
        .filled_qty = 0.0,
        .avg_price  = 0.0,
        .fees_paid  = 0.0,
        .slippage_bps= 0.0,
        .latency_ms = OR_VENUES[ex->id].latency_ms,
    };

    Order aggressor = {
        .id         = or_next_id(),
        .side       = child->side,
        .type       = (child->limit_price > 0.0) ? TYPE_LIMIT : TYPE_MARKET,
        .status     = STATUS_OPEN,
        .quantity   = child->quantity,
        .price      = child->limit_price,
        .filled_qty = 0.0,
    };
    result.order_id = aggressor.id;

    int n_before = ex->book.n_trades;
    or_book_add_order(&ex->book, &aggressor);
    int n_new = ex->book.n_trades - n_before;

    if (n_new == 0) return result;

    /* Compute VWAP fill price and fees */
    double notional = 0.0;
    double filled   = 0.0;
    for (int i = n_before; i < ex->book.n_trades; i++) {
        const Trade *t = &ex->book.trades[i];
        notional += t->price * t->quantity;
        filled   += t->quantity;
    }

    result.filled_qty = filled;
    result.avg_price  = notional / filled;
    result.fees_paid  = result.avg_price * filled *
                        OR_VENUES[ex->id].fee_bps / 10000.0;

    double ref = (ex->ref_price > 0.0) ? ex->ref_price : result.avg_price;
    if (child->side == SIDE_BUY)
        result.slippage_bps = (result.avg_price - ref) / ref * 10000.0;
    else
        result.slippage_bps = (ref - result.avg_price) / ref * 10000.0;

    return result;
}

double or_exchange_quote(const Exchange *ex, OrderSide side) {
    return (side == SIDE_BUY) ? or_book_best_ask(&ex->book)
                              : or_book_best_bid(&ex->book);
}

double or_exchange_effective_price(const Exchange *ex, OrderSide side) {
    double q = or_exchange_quote(ex, side);
    if (q == 0.0) return 0.0;
    double fee = OR_VENUES[ex->id].fee_bps;
    return (side == SIDE_BUY) ? q * (1.0 + fee / 10000.0)
                              : q * (1.0 - fee / 10000.0);
}

double or_exchange_liquidity(const Exchange *ex, OrderSide side) {
    return (side == SIDE_BUY) ? or_book_ask_liquidity(&ex->book)
                              : or_book_bid_liquidity(&ex->book);
}
