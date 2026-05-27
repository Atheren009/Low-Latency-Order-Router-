/*
 * test_order_book.c — 20 assert()-based tests for the order book.
 *
 * Tests mirror test_order_book.py for parity validation.
 * Build: gcc -std=c11 -Wall -I../include test_order_book.c ../src/or_order_book.c -lm -o test_order_book
 * Run:   ./test_order_book
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../include/or_order_book.h"

#define EPSILON 1e-9
#define EQ(a, b) (fabs((a) - (b)) < EPSILON)
#define PASS(name) printf("[PASS] %s\n", name)

static Order make_limit(uint64_t id, OrderSide side, double qty, double price) {
    return (Order){
        .id = id, .side = side, .type = TYPE_LIMIT,
        .status = STATUS_OPEN, .quantity = qty, .price = price, .filled_qty = 0.0
    };
}
static Order make_market(uint64_t id, OrderSide side, double qty) {
    return (Order){
        .id = id, .side = side, .type = TYPE_MARKET,
        .status = STATUS_OPEN, .quantity = qty, .price = 0.0, .filled_qty = 0.0
    };
}

/* T1: Empty book has no best bid/ask */
static void t1_empty_book(void) {
    OrderBook b; or_book_init(&b);
    assert(or_book_best_bid(&b) == 0.0);
    assert(or_book_best_ask(&b) == 0.0);
    PASS("T1 empty book");
}

/* T2: Single resting limit order is visible as best quote */
static void t2_single_resting(void) {
    OrderBook b; or_book_init(&b);
    Order o = make_limit(1, SIDE_BUY, 100, 190.0);
    or_book_rest(&b, &o);
    assert(EQ(or_book_best_bid(&b), 190.0));
    assert(or_book_best_ask(&b) == 0.0);
    PASS("T2 single resting bid");
}

/* T3: Matching bid > ask executes at maker (ask) price */
static void t3_basic_match(void) {
    OrderBook b; or_book_init(&b);
    Order sell = make_limit(1, SIDE_SELL, 100, 189.5);
    or_book_rest(&b, &sell);
    Order buy  = make_limit(2, SIDE_BUY,  100, 190.0);
    or_book_add_order(&b, &buy);

    assert(b.n_trades == 1);
    assert(EQ(b.trades[0].price,    189.5));
    assert(EQ(b.trades[0].quantity, 100.0));
    PASS("T3 basic match at maker price");
}

/* T4: Partial fill — aggressor larger than resting */
static void t4_partial_fill(void) {
    OrderBook b; or_book_init(&b);
    Order sell = make_limit(1, SIDE_SELL, 50, 190.0);
    or_book_rest(&b, &sell);
    Order buy  = make_limit(2, SIDE_BUY, 200, 191.0);
    or_book_add_order(&b, &buy);

    assert(b.n_trades == 1);
    assert(EQ(b.trades[0].quantity, 50.0));
    assert(buy.filled_qty == 50.0);
    assert(buy.status == STATUS_PARTIAL);
    PASS("T4 partial fill");
}

/* T5: Market order sweeps all ask levels */
static void t5_market_sweep(void) {
    OrderBook b; or_book_init(&b);
    Order s1 = make_limit(1, SIDE_SELL, 100, 190.0);
    Order s2 = make_limit(2, SIDE_SELL, 100, 191.0);
    or_book_rest(&b, &s1);
    or_book_rest(&b, &s2);
    Order buy = make_market(3, SIDE_BUY, 200.0);
    or_book_add_order(&b, &buy);

    assert(b.n_trades == 2);
    assert(EQ(buy.filled_qty, 200.0));
    assert(buy.status == STATUS_FILLED);
    PASS("T5 market sweep two levels");
}

/* T6: FIFO priority — earlier order at same price fills first */
static void t6_fifo_priority(void) {
    OrderBook b; or_book_init(&b);
    Order s1 = make_limit(1, SIDE_SELL, 50, 190.0);
    Order s2 = make_limit(2, SIDE_SELL, 50, 190.0);
    or_book_rest(&b, &s1);
    or_book_rest(&b, &s2);
    Order buy = make_market(3, SIDE_BUY, 50.0);
    or_book_add_order(&b, &buy);

    assert(b.n_trades == 1);
    assert(b.trades[0].sell_id == 1);   /* first resting order filled first */
    PASS("T6 FIFO priority");
}

/* T7: Limit buy below ask does NOT match */
static void t7_limit_no_cross(void) {
    OrderBook b; or_book_init(&b);
    Order sell = make_limit(1, SIDE_SELL, 100, 192.0);
    or_book_rest(&b, &sell);
    Order buy  = make_limit(2, SIDE_BUY,  100, 190.0);
    or_book_add_order(&b, &buy);

    assert(b.n_trades == 0);
    assert(EQ(or_book_best_bid(&b), 190.0));
    assert(EQ(or_book_best_ask(&b), 192.0));
    PASS("T7 limit no cross");
}

/* T8: Cancel removes resting order */
static void t8_cancel(void) {
    OrderBook b; or_book_init(&b);
    Order buy = make_limit(10, SIDE_BUY, 100, 190.0);
    or_book_rest(&b, &buy);
    assert(EQ(or_book_best_bid(&b), 190.0));

    OrError e = or_book_cancel(&b, 10, SIDE_BUY);
    assert(e == OR_OK);
    assert(or_book_best_bid(&b) == 0.0);
    PASS("T8 cancel resting order");
}

/* T9: Cancel unknown ID returns OR_ERR_NOT_FOUND */
static void t9_cancel_not_found(void) {
    OrderBook b; or_book_init(&b);
    OrError e = or_book_cancel(&b, 999, SIDE_BUY);
    assert(e == OR_ERR_NOT_FOUND);
    PASS("T9 cancel not found");
}

/* T10: Invalid quantity rejected */
static void t10_invalid_qty(void) {
    OrderBook b; or_book_init(&b);
    Order bad = make_limit(1, SIDE_BUY, -10, 190.0);
    OrError e = or_book_add_order(&b, &bad);
    assert(e == OR_ERR_INVALID_QTY);
    PASS("T10 invalid qty");
}

/* T11: Invalid LIMIT price rejected */
static void t11_invalid_price(void) {
    OrderBook b; or_book_init(&b);
    Order bad = { .id=1, .side=SIDE_BUY, .type=TYPE_LIMIT,
                  .status=STATUS_OPEN, .quantity=100, .price=-1.0 };
    OrError e = or_book_add_order(&b, &bad);
    assert(e == OR_ERR_INVALID_PRICE);
    PASS("T11 invalid limit price");
}

/* T12: Ask liquidity sums resting quantities */
static void t12_ask_liquidity(void) {
    OrderBook b; or_book_init(&b);
    Order s1 = make_limit(1, SIDE_SELL, 100, 190.0);
    Order s2 = make_limit(2, SIDE_SELL, 200, 191.0);
    or_book_rest(&b, &s1);
    or_book_rest(&b, &s2);
    assert(EQ(or_book_ask_liquidity(&b), 300.0));
    PASS("T12 ask liquidity");
}

/* T13: SELL aggressor matches best bid first */
static void t13_sell_hits_bid(void) {
    OrderBook b; or_book_init(&b);
    Order b1 = make_limit(1, SIDE_BUY, 100, 191.0);
    Order b2 = make_limit(2, SIDE_BUY, 100, 190.0);
    or_book_rest(&b, &b1);
    or_book_rest(&b, &b2);
    Order sell = make_market(3, SIDE_SELL, 100.0);
    or_book_add_order(&b, &sell);

    assert(b.n_trades == 1);
    assert(EQ(b.trades[0].price, 191.0));  /* best bid first */
    PASS("T13 SELL hits best bid");
}

/* T14: Multiple price levels, price priority respected */
static void t14_price_priority(void) {
    OrderBook b; or_book_init(&b);
    Order cheap = make_limit(1, SIDE_SELL, 100, 189.0);
    Order dear  = make_limit(2, SIDE_SELL, 100, 190.0);
    or_book_rest(&b, &dear);    /* insert out of price order */
    or_book_rest(&b, &cheap);
    assert(EQ(or_book_best_ask(&b), 189.0));   /* cheaper ask should be best */
    PASS("T14 price priority in sorted book");
}

/* T15: Resting order after partial fill */
static void t15_resting_partial(void) {
    OrderBook b; or_book_init(&b);
    Order sell = make_limit(1, SIDE_SELL, 200, 190.0);
    or_book_rest(&b, &sell);
    Order buy = make_market(2, SIDE_BUY, 50.0);
    or_book_add_order(&b, &buy);

    /* Sell should still be resting with 150 remaining */
    assert(b.n_trades == 1);
    assert(EQ(b.trades[0].quantity, 50.0));
    assert(EQ(or_book_ask_liquidity(&b), 150.0));
    PASS("T15 resting order partially consumed");
}

/* T16: Effective ask includes fee adjustment */
static void t16_effective_ask(void) {
    OrderBook b; or_book_init(&b);
    Order sell = make_limit(1, SIDE_SELL, 100, 190.0);
    or_book_rest(&b, &sell);
    double eff = or_book_effective_ask(&b, 2.0);  /* 2 bps fee */
    assert(EQ(eff, 190.0 * 1.0002));
    PASS("T16 effective ask with fee");
}

/* T17: Effective bid includes fee adjustment */
static void t17_effective_bid(void) {
    OrderBook b; or_book_init(&b);
    Order buy = make_limit(1, SIDE_BUY, 100, 190.0);
    or_book_rest(&b, &buy);
    double eff = or_book_effective_bid(&b, 0.5);  /* 0.5 bps fee */
    assert(EQ(eff, 190.0 * (1.0 - 0.5/10000.0)));
    PASS("T17 effective bid with fee");
}

/* T18: Market order on empty book leaves order unfilled */
static void t18_market_no_liquidity(void) {
    OrderBook b; or_book_init(&b);
    Order buy = make_market(1, SIDE_BUY, 100.0);
    OrError e = or_book_add_order(&b, &buy);
    assert(e == OR_OK);
    assert(b.n_trades == 0);
    assert(buy.filled_qty == 0.0);
    assert(buy.status == STATUS_OPEN);
    PASS("T18 market on empty book");
}

/* T19: Book correctly initialises to zero state */
static void t19_init_zero(void) {
    OrderBook b; or_book_init(&b);
    assert(b.asks.count == 0);
    assert(b.bids.count == 0);
    assert(b.n_trades == 0);
    PASS("T19 init zero state");
}

/* T20: Multiple fills from same level aggregate correctly */
static void t20_multi_fill_same_level(void) {
    OrderBook b; or_book_init(&b);
    Order s1 = make_limit(1, SIDE_SELL,  30, 190.0);
    Order s2 = make_limit(2, SIDE_SELL,  70, 190.0);
    or_book_rest(&b, &s1);
    or_book_rest(&b, &s2);
    Order buy = make_market(3, SIDE_BUY, 100.0);
    or_book_add_order(&b, &buy);

    assert(b.n_trades == 2);
    assert(EQ(buy.filled_qty, 100.0));
    assert(buy.status == STATUS_FILLED);
    PASS("T20 multi fill same price level");
}

int main(void) {
    printf("=== Order Book Tests ===\n");
    t1_empty_book();
    t2_single_resting();
    t3_basic_match();
    t4_partial_fill();
    t5_market_sweep();
    t6_fifo_priority();
    t7_limit_no_cross();
    t8_cancel();
    t9_cancel_not_found();
    t10_invalid_qty();
    t11_invalid_price();
    t12_ask_liquidity();
    t13_sell_hits_bid();
    t14_price_priority();
    t15_resting_partial();
    t16_effective_ask();
    t17_effective_bid();
    t18_market_no_liquidity();
    t19_init_zero();
    t20_multi_fill_same_level();
    printf("=== All 20 tests passed ===\n");
    return 0;
}
