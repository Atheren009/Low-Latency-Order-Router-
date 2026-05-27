/*
 * or_order_book.h — Price-time priority order book API.
 *
 * Storage: fixed sorted arrays (no heap, cache-friendly).
 *   Bids: levels[0] = highest bid (best), sorted descending.
 *   Asks: levels[0] = lowest  ask (best), sorted ascending.
 *   Each level holds a circular FIFO queue of up to OR_MAX_ORDERS_LVL orders.
 */
#ifndef OR_ORDER_BOOK_H
#define OR_ORDER_BOOK_H

#include "or_types.h"

/* ── Price Level (circular FIFO queue) ──────────────────────────────── */
typedef struct {
    double  price;
    Order   orders[OR_MAX_ORDERS_LVL];
    int     front;   /* index of head order in the circular buffer      */
    int     count;   /* number of live orders in the queue              */
} PriceLevel;

/* ── One side of the book (bids or asks) ────────────────────────────── */
typedef struct {
    PriceLevel levels[OR_MAX_LEVELS];
    int        count;    /* number of active price levels               */
    bool       is_bid;   /* true → descending sort; false → ascending   */
} BookSide;

/* ── Order Book ─────────────────────────────────────────────────────── */
typedef struct {
    BookSide asks;
    BookSide bids;
    Trade    trades[OR_MAX_TRADES];
    int      n_trades;
} OrderBook;

/* ── API ────────────────────────────────────────────────────────────── */

/* Initialise (zero-fill) a book. */
void    or_book_init(OrderBook *book);

/* Validate and submit an aggressor order; triggers matching.
 * LIMIT remainder is NOT rested automatically — caller decides.         */
OrError or_book_add_order(OrderBook *book, Order *order);

/* Place a resting order directly (bypass matching). Used for seeding.  */
OrError or_book_rest(OrderBook *book, Order *order);

/* Cancel a resting order by ID. Returns OR_ERR_NOT_FOUND if absent.    */
OrError or_book_cancel(OrderBook *book, uint64_t order_id, OrderSide side);

/* Best quotes (0.0 when side is empty). */
double  or_book_best_ask(const OrderBook *book);
double  or_book_best_bid(const OrderBook *book);

/* Total resting quantity on each side. */
double  or_book_ask_liquidity(const OrderBook *book);
double  or_book_bid_liquidity(const OrderBook *book);

/* Fee-adjusted effective price: ask*(1+fee/10000) or bid*(1-fee/10000) */
double  or_book_effective_ask(const OrderBook *book, double fee_bps);
double  or_book_effective_bid(const OrderBook *book, double fee_bps);

#endif /* OR_ORDER_BOOK_H */
