/*
 * or_csv.h — Minimal CSV bar loader for AAPL/MSFT/SPY 1-minute data.
 *
 * Expected column order:
 *   symbol, timestamp, open, high, low, close, volume, trade_count, vwap
 *
 * The loader skips the header row and any rows whose symbol does not
 * match the requested symbol_filter.  Timestamps are stored as the
 * zero-based row index (int64_t) — the hot path never needs wall time.
 */
#ifndef OR_CSV_H
#define OR_CSV_H

#include "or_types.h"

/* ── Dataset ────────────────────────────────────────────────────────── */
typedef struct {
    Bar    bars[OR_MAX_BARS];
    int    n_bars;
    char   symbol[OR_MAX_SYMBOL];
    char   path[512];
} Dataset;

/*
 * Load bars from a CSV file into ds.
 * Returns number of bars loaded, or -1 on error.
 *
 * @param path           Path to CSV file.
 * @param symbol_filter  Only rows matching this symbol are loaded.
 *                       Pass NULL or "" to load all rows.
 * @param ds             Output dataset (caller-allocated).
 */
int or_csv_load(const char *path, const char *symbol_filter, Dataset *ds);

#endif /* OR_CSV_H */
