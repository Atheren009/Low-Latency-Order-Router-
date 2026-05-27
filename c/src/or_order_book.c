/*
 * or_order_book.c — Price-time priority matching engine implementation.
 *
 * Data layout:
 *   BookSide.levels[] is kept sorted at all times:
 *     asks → ascending price  (levels[0] = cheapest ask)
 *     bids → descending price (levels[0] = highest bid)
 *   Each PriceLevel.orders[] is a circular FIFO (front, count).
 *
 * All operations are O(N) where N = OR_MAX_LEVELS (= 8) — effectively O(1).
 */
#include "or_order_book.h"
#include <string.h>
#include <math.h>

/* ── Internal helpers ────────────────────────────────────────────────── */

/* Return pointer to front order of a level (no bounds check). */
static inline Order *level_front(PriceLevel *lvl) {
    return &lvl->orders[lvl->front];
}

/* Pop the front order (mark slot empty, advance head, decrement count). */
static inline void level_pop_front(PriceLevel *lvl) {
    lvl->front = (lvl->front + 1) % OR_MAX_ORDERS_LVL;
    lvl->count--;
}

/* Append an order to the back of the circular queue. */
static inline OrError level_push_back(PriceLevel *lvl, const Order *o) {
    if (lvl->count >= OR_MAX_ORDERS_LVL) return OR_ERR_BOOK_FULL;
    int back = (lvl->front + lvl->count) % OR_MAX_ORDERS_LVL;
    lvl->orders[back] = *o;
    lvl->count++;
    return OR_OK;
}

/*
 * Binary search for the insertion index of `price` in a BookSide.
 * Bids (is_bid=true):  descending → insert so that higher prices come first.
 * Asks (is_bid=false): ascending  → insert so that lower  prices come first.
 * Returns the index where price would be inserted (existing equal price → same idx).
 */
static int book_find_level(const BookSide *side, double price) {
    int lo = 0, hi = side->count;
    if (side->is_bid) {
        /* Descending: find first position where levels[pos].price <= price */
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (side->levels[mid].price > price) lo = mid + 1;
            else                                 hi = mid;
        }
    } else {
        /* Ascending: find first position where levels[pos].price >= price */
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (side->levels[mid].price < price) lo = mid + 1;
            else                                 hi = mid;
        }
    }
    return lo;
}

/* Remove price level at index i by shifting remaining levels down. */
static void book_remove_level(BookSide *side, int i) {
    int tail = side->count - 1 - i;
    if (tail > 0)
        memmove(&side->levels[i], &side->levels[i + 1],
                sizeof(PriceLevel) * tail);
    side->count--;
}

/* ── Matching engine ─────────────────────────────────────────────────── */

static void book_match(OrderBook *book, Order *aggressor) {
    BookSide *opp = (aggressor->side == SIDE_BUY) ? &book->asks : &book->bids;

    while (or_order_active(aggressor) && opp->count > 0) {
        PriceLevel *level = &opp->levels[0];   /* always best price */
        double best_price  = level->price;

        /* Price eligibility: MARKET always matches; LIMIT checks price */
        if (aggressor->type == TYPE_LIMIT) {
            if (aggressor->side == SIDE_BUY  && aggressor->price < best_price) break;
            if (aggressor->side == SIDE_SELL && aggressor->price > best_price) break;
        }

        /* Fill orders at this price level (FIFO) */
        while (or_order_active(aggressor) && level->count > 0) {
            Order *resting   = level_front(level);
            double fill_qty  = fmin(or_order_remaining(aggressor),
                                    or_order_remaining(resting));

            or_order_apply_fill(aggressor, fill_qty);
            or_order_apply_fill(resting,   fill_qty);

            /* Record trade */
            if (book->n_trades < OR_MAX_TRADES) {
                Trade *t = &book->trades[book->n_trades++];
                t->price    = best_price;
                t->quantity = fill_qty;
                if (aggressor->side == SIDE_BUY) {
                    t->buy_id  = aggressor->id;
                    t->sell_id = resting->id;
                } else {
                    t->buy_id  = resting->id;
                    t->sell_id = aggressor->id;
                }
            }

            /* Remove fully-filled resting order from level */
            if (!or_order_active(resting))
                level_pop_front(level);
        }

        /* Remove empty price level */
        if (level->count == 0)
            book_remove_level(opp, 0);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

void or_book_init(OrderBook *book) {
    memset(book, 0, sizeof(OrderBook));
    book->asks.is_bid = false;
    book->bids.is_bid = true;
}

OrError or_book_rest(OrderBook *book, Order *order) {
    BookSide *side = (order->side == SIDE_BUY) ? &book->bids : &book->asks;

    if (side->count >= OR_MAX_LEVELS) return OR_ERR_BOOK_FULL;

    int pos = book_find_level(side, order->price);

    if (pos < side->count && side->levels[pos].price == order->price) {
        /* Append to existing level */
        return level_push_back(&side->levels[pos], order);
    }

    /* Insert new level */
    if (side->count >= OR_MAX_LEVELS) return OR_ERR_BOOK_FULL;

    int tail = side->count - pos;
    if (tail > 0)
        memmove(&side->levels[pos + 1], &side->levels[pos],
                sizeof(PriceLevel) * tail);

    PriceLevel *lvl = &side->levels[pos];
    memset(lvl, 0, sizeof(PriceLevel));
    lvl->price      = order->price;
    lvl->front      = 0;
    lvl->count      = 1;
    lvl->orders[0]  = *order;
    side->count++;
    return OR_OK;
}

OrError or_book_add_order(OrderBook *book, Order *order) {
    if (order->quantity <= 0.0)       return OR_ERR_INVALID_QTY;
    if (order->type == TYPE_LIMIT &&
        order->price <= 0.0)          return OR_ERR_INVALID_PRICE;

    book_match(book, order);

    /* Only resting LIMIT orders that are still active stay in the book */
    if (order->type == TYPE_LIMIT && or_order_active(order))
        return or_book_rest(book, order);

    return OR_OK;
}

OrError or_book_cancel(OrderBook *book, uint64_t order_id, OrderSide side) {
    BookSide *bs = (side == SIDE_BUY) ? &book->bids : &book->asks;
    for (int i = 0; i < bs->count; i++) {
        PriceLevel *lvl = &bs->levels[i];
        for (int j = 0; j < lvl->count; j++) {
            int idx = (lvl->front + j) % OR_MAX_ORDERS_LVL;
            if (lvl->orders[idx].id == order_id) {
                lvl->orders[idx].status = STATUS_CANCELLED;
                /* Shift remaining orders to fill the hole */
                for (int k = j; k < lvl->count - 1; k++) {
                    int a = (lvl->front + k)     % OR_MAX_ORDERS_LVL;
                    int b = (lvl->front + k + 1) % OR_MAX_ORDERS_LVL;
                    lvl->orders[a] = lvl->orders[b];
                }
                lvl->count--;
                if (lvl->count == 0) book_remove_level(bs, i);
                return OR_OK;
            }
        }
    }
    return OR_ERR_NOT_FOUND;
}

double or_book_best_ask(const OrderBook *book) {
    if (book->asks.count == 0) return 0.0;
    return book->asks.levels[0].price;
}

double or_book_best_bid(const OrderBook *book) {
    if (book->bids.count == 0) return 0.0;
    return book->bids.levels[0].price;
}

double or_book_ask_liquidity(const OrderBook *book) {
    double total = 0.0;
    for (int i = 0; i < book->asks.count; i++) {
        const PriceLevel *lvl = &book->asks.levels[i];
        for (int j = 0; j < lvl->count; j++) {
            int idx = (lvl->front + j) % OR_MAX_ORDERS_LVL;
            total += or_order_remaining(&lvl->orders[idx]);
        }
    }
    return total;
}

double or_book_bid_liquidity(const OrderBook *book) {
    double total = 0.0;
    for (int i = 0; i < book->bids.count; i++) {
        const PriceLevel *lvl = &book->bids.levels[i];
        for (int j = 0; j < lvl->count; j++) {
            int idx = (lvl->front + j) % OR_MAX_ORDERS_LVL;
            total += or_order_remaining(&lvl->orders[idx]);
        }
    }
    return total;
}

double or_book_effective_ask(const OrderBook *book, double fee_bps) {
    double ask = or_book_best_ask(book);
    if (ask == 0.0) return 0.0;
    return ask * (1.0 + fee_bps / 10000.0);
}

double or_book_effective_bid(const OrderBook *book, double fee_bps) {
    double bid = or_book_best_bid(book);
    if (bid == 0.0) return 0.0;
    return bid * (1.0 - fee_bps / 10000.0);
}
