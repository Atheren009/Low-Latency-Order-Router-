/*
 * or_types.h — All shared types, enums, structs, and venue config.
 *
 * Design rules:
 *   - All structs are plain C11, no heap allocation on the hot path.
 *   - Order IDs are uint64_t monotonic counters (no UUID, no malloc).
 *   - Venue config is a compile-time static array (no hash map).
 *   - Timestamps are int64_t nanoseconds since Unix epoch.
 */
#ifndef OR_TYPES_H
#define OR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

/* ── Capacity constants ─────────────────────────────────────────────── */
#define OR_MAX_LEVELS      8    /* price levels per book side            */
#define OR_MAX_ORDERS_LVL 16    /* orders in the FIFO queue per level    */
#define OR_MAX_TRADES    512    /* trade ring buffer per book            */
#define OR_MAX_CHILDREN    8    /* child orders per routing tranche      */
#define OR_MAX_TRANCHES   10    /* tranches per parent order             */
#define OR_VENUE_COUNT     3    /* ALPHA=0, BETA=1, GAMMA=2              */
#define OR_MAX_BARS     5000    /* max bars loaded per dataset           */
#define OR_MAX_SYMBOL      8    /* symbol string length incl. NUL        */

/* ── Enumerations ───────────────────────────────────────────────────── */
typedef enum { SIDE_BUY = 0, SIDE_SELL = 1 }                   OrderSide;
typedef enum { TYPE_LIMIT = 0, TYPE_MARKET = 1 }                OrderType;
typedef enum {
    STATUS_OPEN = 0, STATUS_PARTIAL = 1,
    STATUS_FILLED = 2, STATUS_CANCELLED = 3,
}                                                               OrderStatus;
typedef enum { VENUE_ALPHA = 0, VENUE_BETA = 1, VENUE_GAMMA = 2 } VenueId;

/* ── Error codes ────────────────────────────────────────────────────── */
typedef enum {
    OR_OK                = 0,
    OR_ERR_INVALID_QTY   = 1,
    OR_ERR_INVALID_PRICE = 2,
    OR_ERR_NOT_FOUND     = 3,
    OR_ERR_BOOK_FULL     = 4,
    OR_ERR_NO_LIQUIDITY  = 5,
    OR_ERR_NO_BARS       = 6,
} OrError;

/* ── Order ──────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t    id;           /* monotonic counter; 0 = uninitialized   */
    OrderSide   side;
    OrderType   type;
    OrderStatus status;
    double      quantity;
    double      price;        /* 0.0 for MARKET orders                  */
    double      filled_qty;
} Order;

static inline double or_order_remaining(const Order *o) {
    return o->quantity - o->filled_qty;
}
static inline bool or_order_active(const Order *o) {
    return o->status == STATUS_OPEN || o->status == STATUS_PARTIAL;
}
static inline void or_order_apply_fill(Order *o, double qty) {
    o->filled_qty += qty;
    if (o->filled_qty >= o->quantity) {
        o->filled_qty = o->quantity;
        o->status = STATUS_FILLED;
    } else {
        o->status = STATUS_PARTIAL;
    }
}

/* ── Trade ──────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t buy_id;
    uint64_t sell_id;
    double   price;
    double   quantity;
} Trade;

/* ── Bar (OHLCV 1-minute) ───────────────────────────────────────────── */
typedef struct {
    int64_t  row_index;       /* bar number in the dataset (0-based)    */
    double   open;
    double   high;
    double   low;
    double   close;
    double   volume;
    double   trade_count;
    double   vwap;
} Bar;

/* ── FillResult ─────────────────────────────────────────────────────── */
typedef struct {
    VenueId  venue;
    uint64_t order_id;
    double   filled_qty;
    double   avg_price;
    double   fees_paid;
    double   slippage_bps;
    int      latency_ms;
} FillResult;

/* ── ChildOrder ─────────────────────────────────────────────────────── */
typedef struct {
    uint64_t   parent_id;
    VenueId    venue;
    OrderSide  side;
    double     quantity;
    double     limit_price;   /* 0.0 = submit as MARKET                 */
    FillResult fill;
    bool       has_fill;
} ChildOrder;

/* ── Venue static configuration ─────────────────────────────────────── */
typedef struct {
    const char *name;
    double      fee_bps;
    int         latency_ms;
    double      spread_bias;
} VenueConfig;

static const VenueConfig OR_VENUES[OR_VENUE_COUNT] = {
    [VENUE_ALPHA] = { "ALPHA", 2.0,  1,  0.01 },
    [VENUE_BETA]  = { "BETA",  1.0,  5,  0.03 },
    [VENUE_GAMMA] = { "GAMMA", 0.5, 15,  0.05 },
};

/* ── Monotonic order ID ─────────────────────────────────────────────── */
static _Atomic uint64_t _or_id_counter = 1;
static inline uint64_t or_next_id(void) {
    return atomic_fetch_add_explicit(&_or_id_counter, 1,
                                     memory_order_relaxed);
}

/* ── Routing result (aggregated across all tranches) ────────────────── */
typedef struct {
    double   total_filled_qty;
    double   avg_fill_price;      /* VWAP of all fills                  */
    double   total_fees_paid;
    double   slippage_bps;        /* vs reference price                 */
    double   fill_rate_pct;       /* filled / requested * 100           */
    int      n_trades;
    int      total_latency_ms;
} RouteResult;

#endif /* OR_TYPES_H */
