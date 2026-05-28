/* CSV bar loader — expects: symbol, timestamp, open, high, low, close, volume, trade_count, vwap */
#ifndef OR_CSV_H
#define OR_CSV_H

#include "or_types.h"

typedef struct {
    Bar    bars[OR_MAX_BARS];
    int    n_bars;
    char   symbol[OR_MAX_SYMBOL];
    char   path[512];
} Dataset;

/* Loads bars matching symbol_filter (NULL/"" = all). Returns count or -1. */
int or_csv_load(const char *path, const char *symbol_filter, Dataset *ds);

#endif /* OR_CSV_H */
