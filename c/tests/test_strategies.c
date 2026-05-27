/*
 * test_strategies.c — Routing strategy tests (15 tests).
 *
 * Build: gcc -std=c11 -Wall -I../include test_strategies.c
 *         ../src/or_order_book.c ../src/or_exchange.c ../src/or_routing.c
 *         ../src/or_strategy_best_price.c ../src/or_strategy_smart.c
 *         ../src/or_strategy_twap.c ../src/or_strategy_vwap.c -lm -o test_strategies
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../include/or_routing.h"

#define EQ(a,b) (fabs((a)-(b)) < 1e-6)
#define PASS(n) printf("[PASS] %s\n", n)

static Bar ref_bar(void) {
    return (Bar){ .row_index=0, .open=189.5, .high=190.2, .low=189.3,
                  .close=189.95, .volume=8500.0, .trade_count=120, .vwap=189.82 };
}

static void seed_all(Exchange venues[OR_VENUE_COUNT], const Bar *b) {
    for (int v = 0; v < OR_VENUE_COUNT; v++) {
        or_exchange_init(&venues[v], (VenueId)v);
        or_exchange_seed(&venues[v], b);
    }
}

static Order make_parent(double qty) {
    return (Order){ .id=or_next_id(), .side=SIDE_BUY, .type=TYPE_MARKET,
                    .status=STATUS_OPEN, .quantity=qty };
}

/* S1: BestPrice routes to single venue */
static void s1_best_price_single_venue(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_best_price();
    Order parent = make_parent(100.0);

    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);

    assert(n_t == 1);
    assert(out_n[0] == 1);
    assert(EQ(out[0][0].quantity, 100.0));
    PASS("S1 BestPrice single venue");
}

/* S2: BestPrice selects cheapest effective price */
static void s2_best_price_cheapest(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_best_price();
    Order parent = make_parent(100.0);
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);

    /* ALPHA has tightest spread but highest fee.
     * Effective ask: ALPHA = 189.83 * 1.0002 = 189.868
     *                BETA  = 189.85 * 1.0001 = 189.869
     *                GAMMA = 189.87 * 1.00005 = 189.879
     * So ALPHA should win (cheapest effective price). */
    VenueId chosen = out[0][0].venue;
    double chosen_eff = or_exchange_effective_price(&venues[chosen], SIDE_BUY);
    for (int v = 0; v < OR_VENUE_COUNT; v++) {
        double eff = or_exchange_effective_price(&venues[v], SIDE_BUY);
        assert(eff >= chosen_eff);
    }
    PASS("S2 BestPrice selects cheapest effective price");
}

/* S3: Smart routes across multiple venues when qty > single venue depth */
static void s3_smart_multi_venue(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_smart(1.0);
    /* 5000 shares; each venue has ~850 shares (8500/30*3) → needs all venues */
    Order parent = make_parent(5000.0);
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);

    assert(n_t == 1);
    assert(out_n[0] > 1);   /* multiple children needed */
    PASS("S3 Smart multi-venue for large order");
}

/* S4: Smart total allocation = order quantity */
static void s4_smart_total_qty(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_smart(1.0);
    Order parent = make_parent(500.0);
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);

    double total = 0.0;
    for (int c = 0; c < out_n[0]; c++) total += out[0][c].quantity;
    assert(EQ(total, 500.0));
    PASS("S4 Smart total allocation = order qty");
}

/* S5: TWAP returns num_slices tranches */
static void s5_twap_n_tranches(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_twap(5);
    Order parent = make_parent(1000.0);
    Bar bars[5]; for (int i=0;i<5;i++) bars[i]=b;
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, bars, 5, s.params, out, out_n, &n_t);

    assert(n_t == 5);
    PASS("S5 TWAP returns 5 tranches");
}

/* S6: TWAP equal slice sizes */
static void s6_twap_equal_slices(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_twap(5);
    Order parent = make_parent(1000.0);
    Bar bars[5]; for (int i=0;i<5;i++) bars[i]=b;
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, bars, 5, s.params, out, out_n, &n_t);

    for (int t = 0; t < 5; t++) {
        if (out_n[t] > 0)
            assert(EQ(out[t][0].quantity, 200.0));  /* 1000/5 = 200 */
    }
    PASS("S6 TWAP equal slice sizes");
}

/* S7: TWAP needs bars — OR_ERR_NO_BARS without them */
static void s7_twap_no_bars(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_twap(5);
    Order parent = make_parent(1000.0);
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    OrError e = s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);
    assert(e == OR_ERR_NO_BARS);
    PASS("S7 TWAP returns OR_ERR_NO_BARS without bars");
}

/* S8: VWAP returns num_slices tranches */
static void s8_vwap_n_tranches(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_vwap(5, 1.0);
    Order parent = make_parent(1000.0);
    Bar bars[5]; for(int i=0;i<5;i++) { bars[i]=b; bars[i].volume = 1000.0 + i*200; }
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, bars, 5, s.params, out, out_n, &n_t);
    assert(n_t == 5);
    PASS("S8 VWAP returns 5 tranches");
}

/* S9: VWAP total = order qty */
static void s9_vwap_total_qty(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_vwap(5, 1.0);
    Order parent = make_parent(1000.0);
    Bar bars[5];
    for(int i=0;i<5;i++) { bars[i]=b; bars[i].volume = 100.0 * (i+1); }
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, bars, 5, s.params, out, out_n, &n_t);

    double total = 0.0;
    for (int t=0; t<n_t; t++)
        for (int c=0; c<out_n[t]; c++) total += out[t][c].quantity;
    assert(EQ(total, 1000.0));
    PASS("S9 VWAP total quantity = parent qty");
}

/* S10: VWAP volume-weighted: higher-volume bars get more qty */
static void s10_vwap_weights(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_vwap(2, 1.0);
    Order parent = make_parent(1000.0);
    Bar bars[2];
    bars[0] = b; bars[0].volume = 300.0;
    bars[1] = b; bars[1].volume = 700.0;
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, bars, 2, s.params, out, out_n, &n_t);

    /* slice0 = 1000 * 300/1000 = 300; slice1 = 700 */
    assert(EQ(out[0][0].quantity, 300.0));
    assert(EQ(out[1][0].quantity, 700.0));
    PASS("S10 VWAP volume-weighted slices");
}

/* S11: Venue ranking — ascending effective price for BUY */
static void s11_venue_ranking(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    VenueId ranked[OR_VENUE_COUNT];
    int n = or_rank_venues(venues, SIDE_BUY, ranked);
    assert(n == OR_VENUE_COUNT);
    /* Each subsequent venue must have >= effective price */
    for (int i = 0; i < n-1; i++) {
        double a = or_exchange_effective_price(&venues[ranked[i]], SIDE_BUY);
        double b2 = or_exchange_effective_price(&venues[ranked[i+1]], SIDE_BUY);
        assert(a <= b2);
    }
    PASS("S11 venue ranking ascending for BUY");
}

/* S12: No venues with quotes → empty tranche */
static void s12_no_quotes(void) {
    Exchange venues[OR_VENUE_COUNT];
    for (int v=0;v<OR_VENUE_COUNT;v++) or_exchange_init(&venues[v],(VenueId)v);
    /* Don't seed — no liquidity */
    Strategy s = or_strategy_best_price();
    Order parent = make_parent(100.0);
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);
    assert(n_t == 1 && out_n[0] == 0);
    PASS("S12 no quotes → empty tranche");
}

/* S13: TWAP adapts to fewer bars than num_slices */
static void s13_twap_fewer_bars(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy s = or_strategy_twap(5);
    Order parent = make_parent(600.0);
    Bar bars[3]; for(int i=0;i<3;i++) bars[i]=b;
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, bars, 3, s.params, out, out_n, &n_t);
    assert(n_t == 3);
    PASS("S13 TWAP adapts to fewer bars");
}

/* S14: Smart min_qty filter skips low-liquidity venues */
static void s14_smart_min_qty_filter(void) {
    Exchange venues[OR_VENUE_COUNT];
    /* Seed only ALPHA with minimal volume (10 shares per level = 30 total) */
    for (int v=0;v<OR_VENUE_COUNT;v++) or_exchange_init(&venues[v],(VenueId)v);
    Bar b = ref_bar(); b.volume = 0.0;  /* min qty = 10 per level */
    for (int v=0;v<OR_VENUE_COUNT;v++) or_exchange_seed(&venues[v],&b);

    /* min_qty=50 > 30 available → should still try (30 < 50, skip) */
    Strategy s = or_strategy_smart(50.0);
    Order parent = make_parent(100.0);
    ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
    int out_n[OR_MAX_TRANCHES]; int n_t = 0;
    s.route(&parent, venues, NULL, 0, s.params, out, out_n, &n_t);
    /* All venues have 30 liquidity < 50 min_qty → empty tranche */
    assert(out_n[0] == 0);
    PASS("S14 Smart min_qty filter");
}

/* S15: All strategies produce SIDE_BUY children matching parent side */
static void s15_child_side_matches_parent(void) {
    Exchange venues[OR_VENUE_COUNT]; Bar b = ref_bar(); seed_all(venues, &b);
    Strategy strats[4] = {
        or_strategy_best_price(), or_strategy_smart(1.0),
        or_strategy_twap(5), or_strategy_vwap(5, 1.0)
    };
    Bar bars5[5]; for(int i=0;i<5;i++) bars5[i]=b;

    for (int si=0; si<4; si++) {
        seed_all(venues, &b);
        Order parent = make_parent(500.0);
        ChildOrder out[OR_MAX_TRANCHES][OR_MAX_CHILDREN];
        int out_n[OR_MAX_TRANCHES]; int n_t = 0;
        strats[si].route(&parent, venues, bars5, 5, strats[si].params, out, out_n, &n_t);
        for (int t=0; t<n_t; t++)
            for (int c=0; c<out_n[t]; c++)
                assert(out[t][c].side == SIDE_BUY);
    }
    PASS("S15 all strategies produce BUY children for BUY parent");
}

int main(void) {
    printf("=== Strategy Tests ===\n");
    s1_best_price_single_venue();
    s2_best_price_cheapest();
    s3_smart_multi_venue();
    s4_smart_total_qty();
    s5_twap_n_tranches();
    s6_twap_equal_slices();
    s7_twap_no_bars();
    s8_vwap_n_tranches();
    s9_vwap_total_qty();
    s10_vwap_weights();
    s11_venue_ranking();
    s12_no_quotes();
    s13_twap_fewer_bars();
    s14_smart_min_qty_filter();
    s15_child_side_matches_parent();
    printf("=== All 15 strategy tests passed ===\n");
    return 0;
}
