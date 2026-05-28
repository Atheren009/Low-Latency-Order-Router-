/* CSV bar loader (no dynamic allocation).
 * Columns: symbol, timestamp, open, high, low, close, volume, trade_count, vwap */
#include "or_csv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUF 512

int or_csv_load(const char *path, const char *symbol_filter, Dataset *ds) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    ds->n_bars = 0;
    strncpy(ds->path, path, sizeof(ds->path) - 1);
    ds->path[sizeof(ds->path) - 1] = '\0';

    if (symbol_filter && symbol_filter[0]) {
        strncpy(ds->symbol, symbol_filter, sizeof(ds->symbol) - 1);
        ds->symbol[sizeof(ds->symbol) - 1] = '\0';
    } else {
        ds->symbol[0] = '\0';
    }

    char line[LINE_BUF];
    int  row = 0;   /* 0 = header, 1+ = data */
    int64_t bar_index = 0;

    while (fgets(line, sizeof(line), f)) {

        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (row == 0) { row++; continue; }


        char *save = NULL;
        char *tok[9];
        int   nc = 0;
        char *p  = strtok_r(line, ",", &save);
        while (p && nc < 9) { tok[nc++] = p; p = strtok_r(NULL, ",", &save); }
        if (nc < 9) { row++; continue; }


        if (symbol_filter && symbol_filter[0] &&
            strncmp(tok[0], symbol_filter, OR_MAX_SYMBOL - 1) != 0) {
            row++;
            continue;
        }

        if (ds->n_bars >= OR_MAX_BARS) break;

        Bar *b       = &ds->bars[ds->n_bars++];
        b->row_index = bar_index++;
        b->open      = strtod(tok[2], NULL);
        b->high      = strtod(tok[3], NULL);
        b->low       = strtod(tok[4], NULL);
        b->close     = strtod(tok[5], NULL);
        b->volume    = strtod(tok[6], NULL);
        b->trade_count = strtod(tok[7], NULL);
        b->vwap      = strtod(tok[8], NULL);

        row++;
    }

    fclose(f);
    return ds->n_bars;
}
