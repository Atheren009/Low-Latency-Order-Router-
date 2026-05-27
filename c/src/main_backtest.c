/*
 * main_backtest.c — Entry point for the C backtest binary.
 *
 * Usage:
 *   ./build/or_backtest <price_feed_dir> <output_csv> [max_windows]
 *
 * Example:
 *   ./build/or_backtest "Price Feed" results/c_backtest_results.csv 1000
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/or_backtest.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s <price_feed_dir> <output_csv> [max_windows]\n"
            "\nExample:\n"
            "  %s 'Price Feed' results/c_backtest_results.csv 1000\n",
            argv[0], argv[0]);
        return 1;
    }

    const char *feed_dir = argv[1];
    const char *out_csv  = argv[2];
    int max_windows      = (argc >= 4) ? atoi(argv[3]) : 0;

    /* Build dataset paths */
    char path_aapl[600], path_msft[600], path_spy[600];
    snprintf(path_aapl, sizeof(path_aapl), "%s/AAPL_1min_2024-2026.csv", feed_dir);
    snprintf(path_msft, sizeof(path_msft), "%s/MSFT_1min_2024-2026.csv", feed_dir);
    snprintf(path_spy,  sizeof(path_spy),  "%s/SPY_1min_2024-2026.csv",  feed_dir);

    BacktestConfig cfg = {
        .dataset_paths   = { path_aapl, path_msft, path_spy },
        .symbol_filters  = { "AAPL", "MSFT", "SPY" },
        .output_csv      = out_csv,
        .max_windows     = max_windows,
        .n_slices        = 5,
    };

    fprintf(stderr, "[or_backtest] Starting backtest...\n");
    fprintf(stderr, "  AAPL: %s\n", path_aapl);
    fprintf(stderr, "  MSFT: %s\n", path_msft);
    fprintf(stderr, "  SPY:  %s\n", path_spy);
    fprintf(stderr, "  Output: %s\n", out_csv);
    fprintf(stderr, "  Max windows: %s\n", max_windows ? argv[3] : "all");

    int rows = or_backtest_run(&cfg);
    if (rows < 0) {
        fprintf(stderr, "[or_backtest] FAILED.\n");
        return 1;
    }
    fprintf(stderr, "[or_backtest] Complete. %d result rows.\n", rows);
    return 0;
}
