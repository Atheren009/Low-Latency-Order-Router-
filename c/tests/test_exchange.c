/*
 * test_exchange.c — Exchange seeding and submission tests.
 *
 * Build: gcc -std=c11 -Wall -I../include test_exchange.c
 *              ../src/or_order_book.c ../src/or_exchange.c -lm -o test_exchange
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../include/or_exchange.h"

#define EQ(a,b) (fabs((a)-(b)) < 1e-6)
#define PASS(n) printf("[PASS] %s\n", n)

static Bar make_bar(double vwap, double volume) {
    return (Bar){ .row_index=0, .open=vwap, .high=vwap+0.5, .low=vwap-0.5,
                  .close=vwap, .volume=volume, .trade_count=100, .vwap=vwap };
}

/* E1: Seeded book has 3 ask + 3 bid levels */
static void e1_seeding_levels(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);
    assert(ex.book.asks.count == 3);
    assert(ex.book.bids.count == 3);
    PASS("E1 seeding creates 3 ask + 3 bid levels");
}

/* E2: Ask base = vwap + spread_bias for ALPHA (0.01) */
static void e2_alpha_ask_base(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);
    /* best ask = 190.0 + 0.01 = 190.01 */
    assert(EQ(or_exchange_quote(&ex, SIDE_BUY), 190.01));
    PASS("E2 ALPHA ask base = vwap + spread_bias");
}

/* E3: GAMMA ask base further from VWAP (spread_bias=0.05) */
static void e3_gamma_ask_base(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_GAMMA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);
    assert(EQ(or_exchange_quote(&ex, SIDE_BUY), 190.05));
    PASS("E3 GAMMA ask = vwap + 0.05");
}

/* E4: Level quantity = max(10, volume/30) */
static void e4_level_qty(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);
    /* 9000/30 = 300 per level, 3 levels → liquidity = 900 */
    assert(EQ(or_exchange_liquidity(&ex, SIDE_BUY), 900.0));
    PASS("E4 liquidity = 3 * max(10, vol/30)");
}

/* E5: Minimum liquidity when volume is tiny */
static void e5_min_qty(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_BETA);
    Bar b = make_bar(190.0, 0.0);   /* zero volume → min 10 per level */
    or_exchange_seed(&ex, &b);
    assert(EQ(or_exchange_liquidity(&ex, SIDE_BUY), 30.0));  /* 3*10 */
    PASS("E5 min qty = 10 per level when volume=0");
}

/* E6: submit() returns filled_qty and avg_price */
static void e6_submit_fill(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);

    ChildOrder child = { .parent_id=1, .venue=VENUE_ALPHA, .side=SIDE_BUY,
                         .quantity=100.0, .limit_price=0.0 };
    FillResult fr = or_exchange_submit(&ex, &child);

    assert(fr.filled_qty == 100.0);
    assert(fr.avg_price > 0.0);
    PASS("E6 submit fills quantity");
}

/* E7: fees_paid = avg_price * qty * fee_bps / 10000 */
static void e7_fees(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);  /* fee=2.0 bps */
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);

    ChildOrder child = { .parent_id=1, .venue=VENUE_ALPHA, .side=SIDE_BUY,
                         .quantity=100.0, .limit_price=0.0 };
    FillResult fr = or_exchange_submit(&ex, &child);

    double expected_fee = fr.avg_price * 100.0 * 2.0 / 10000.0;
    assert(EQ(fr.fees_paid, expected_fee));
    PASS("E7 fees = price * qty * fee_bps / 10000");
}

/* E8: effective_price = ask * (1 + fee_bps/10000) for BUY */
static void e8_effective_price(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);

    double ask = or_exchange_quote(&ex, SIDE_BUY);
    double eff = or_exchange_effective_price(&ex, SIDE_BUY);
    assert(EQ(eff, ask * (1.0 + 2.0 / 10000.0)));
    PASS("E8 effective_price for BUY");
}

/* E9: Reset clears book */
static void e9_reset(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b = make_bar(190.0, 9000.0);
    or_exchange_seed(&ex, &b);
    assert(or_exchange_liquidity(&ex, SIDE_BUY) > 0.0);
    or_exchange_reset(&ex);
    assert(or_exchange_liquidity(&ex, SIDE_BUY) == 0.0);
    PASS("E9 reset clears book");
}

/* E10: Re-seeding replaces old liquidity */
static void e10_reseed(void) {
    Exchange ex; or_exchange_init(&ex, VENUE_ALPHA);
    Bar b1 = make_bar(190.0, 9000.0);
    Bar b2 = make_bar(200.0, 3000.0);
    or_exchange_seed(&ex, &b1);
    double ask1 = or_exchange_quote(&ex, SIDE_BUY);
    or_exchange_seed(&ex, &b2);
    double ask2 = or_exchange_quote(&ex, SIDE_BUY);
    assert(ask2 > ask1);   /* new bar has higher vwap */
    PASS("E10 reseed updates quotes");
}

int main(void) {
    printf("=== Exchange Tests ===\n");
    e1_seeding_levels();
    e2_alpha_ask_base();
    e3_gamma_ask_base();
    e4_level_qty();
    e5_min_qty();
    e6_submit_fill();
    e7_fees();
    e8_effective_price();
    e9_reset();
    e10_reseed();
    printf("=== All 10 exchange tests passed ===\n");
    return 0;
}
