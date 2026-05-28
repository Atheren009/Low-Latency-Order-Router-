/* Price-time priority order book — fixed sorted arrays, no heap */
#ifndef OR_ORDER_BOOK_H
#define OR_ORDER_BOOK_H

#include "or_types.h"

typedef struct {
    double  price;
    Order   orders[OR_MAX_ORDERS_LVL];
    int     front;   /* circular buffer head                              */
    int     count;
} PriceLevel;

typedef struct {
    PriceLevel levels[OR_MAX_LEVELS];
    int        count;
    bool       is_bid;   /* true → descending sort; false → ascending    */
} BookSide;

typedef struct {
    BookSide asks;
    BookSide bids;
    Trade    trades[OR_MAX_TRADES];
    int      n_trades;
} OrderBook;

void    or_book_init(OrderBook *book);

/* LIMIT remainder is NOT rested — caller decides */
OrError or_book_add_order(OrderBook *book, Order *order);

/* Bypass matching, place resting order directly. Used for seeding. */
OrError or_book_rest(OrderBook *book, Order *order);

OrError or_book_cancel(OrderBook *book, uint64_t order_id, OrderSide side);

/* 0.0 when side is empty */
double  or_book_best_ask(const OrderBook *book);
double  or_book_best_bid(const OrderBook *book);

double  or_book_ask_liquidity(const OrderBook *book);
double  or_book_bid_liquidity(const OrderBook *book);

/* ask*(1+fee/10000) or bid*(1-fee/10000) */
double  or_book_effective_ask(const OrderBook *book, double fee_bps);
double  or_book_effective_bid(const OrderBook *book, double fee_bps);

#endif /* OR_ORDER_BOOK_H */
