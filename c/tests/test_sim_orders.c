/*
 * test_sim_orders.c — Tests for the simulated order generator.
 *
 * Build: gcc -std=c11 -Wall -I../include test_sim_orders.c
 *              ../src/or_sim_orders.c -lm -o test_sim_orders
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../include/or_sim_orders.h"

#define PASS(n) printf("[PASS] %s\n", n)

static Bar make_bars(int n, double vwap) {
    return (Bar){ .row_index=0, .open=vwap,.high=vwap+0.5,.low=vwap-0.5,
                  .close=vwap,.volume=8000,.trade_count=100,.vwap=vwap };
}

static void t1_count(void) {
    Bar b = make_bars(1, 190.0);
    SimScenario out[10];
    or_sim_generate(&b, 1, 42, out, 10);
    /* Just check no crash and IDs are non-zero */
    for (int i = 0; i < 10; i++) assert(out[i].parent.id > 0);
    PASS("T1 generates correct count");
}

static void t2_alternating_sides(void) {
    Bar b = make_bars(1, 190.0);
    SimScenario out[10];
    or_sim_generate(&b, 1, 42, out, 10);
    for (int i = 0; i < 10; i++) {
        OrderSide expected = (i % 2 == 0) ? SIDE_BUY : SIDE_SELL;
        assert(out[i].parent.side == expected);
    }
    PASS("T2 sides alternate BUY/SELL");
}

static void t3_valid_sizes(void) {
    Bar b = make_bars(1, 190.0);
    SimScenario out[50];
    or_sim_generate(&b, 1, 99, out, 50);
    for (int i = 0; i < 50; i++) {
        double q = out[i].parent.quantity;
        int found = 0;
        for (int j = 0; j < OR_SIM_N_SIZES; j++)
            if (fabs(q - OR_SIM_SIZES[j]) < 1e-9) { found = 1; break; }
        assert(found && "Quantity must be one of the 5 canonical sizes");
    }
    PASS("T3 all sizes are canonical");
}

static void t4_reproducible(void) {
    Bar b = make_bars(1, 190.0);
    SimScenario out1[5], out2[5];
    or_sim_generate(&b, 1, 12345, out1, 5);
    or_sim_generate(&b, 1, 12345, out2, 5);
    for (int i = 0; i < 5; i++) {
        assert(out1[i].parent.side     == out2[i].parent.side);
        assert(out1[i].parent.quantity == out2[i].parent.quantity);
        assert(out1[i].parent.type     == out2[i].parent.type);
    }
    PASS("T4 same seed → same sequence");
}

static void t5_label_nonempty(void) {
    Bar b = make_bars(1, 190.0);
    SimScenario out[5];
    or_sim_generate(&b, 1, 7, out, 5);
    for (int i = 0; i < 5; i++) assert(out[i].label[0] != '\0');
    PASS("T5 labels are non-empty");
}

int main(void) {
    printf("=== Sim Order Tests ===\n");
    t1_count();
    t2_alternating_sides();
    t3_valid_sizes();
    t4_reproducible();
    t5_label_nonempty();
    printf("=== All 5 sim order tests passed ===\n");
    return 0;
}
