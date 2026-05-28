/* Shared types, enums, structs, and venue config — all plain C11, no heap on hot path */
#ifndef OR_TYPES_H
#define OR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#define OR_MAX_LEVELS      8
#define OR_MAX_ORDERS_LVL 16
#define OR_MAX_TRADES    512
#define OR_MAX_CHILDREN    8
#define OR_MAX_TRANCHES   10
#define OR_VENUE_COUNT     3    /* ALPHA=0, BETA=1, GAMMA=2              */
#define OR_MAX_BARS     5000
#define OR_MAX_SYMBOL      8    /* incl. NUL                             */

typedef enum { SIDE_BUY = 0, SIDE_SELL = 1 }                   OrderSide;
typedef enum { TYPE_LIMIT = 0, TYPE_MARKET = 1 }                OrderType;
typedef enum {
    STATUS_OPEN = 0, STATUS_PARTIAL = 1,
    STATUS_FILLED = 2, STATUS_CANCELLED = 3,
}                                                               OrderStatus;
typedef enum { VENUE_ALPHA = 0, VENUE_BETA = 1, VENUE_GAMMA = 2 } VenueId;

typedef enum {
    OR_OK                = 0,
    OR_ERR_INVALID_QTY   = 1,
    OR_ERR_INVALID_PRICE = 2,
    OR_ERR_NOT_FOUND     = 3,
    OR_ERR_BOOK_FULL     = 4,
    OR_ERR_NO_LIQUIDITY  = 5,
    OR_ERR_NO_BARS       = 6,
} OrError;

typedef struct {
    uint64_t    id;           /* 0 = uninitialized                       */
    OrderSide   side;
    OrderType   type;
    OrderStatus status;
    double      quantity;
    double      price;        /* 0.0 for MARKET orders                   */
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

typedef struct {
    uint64_t buy_id;
    uint64_t sell_id;
    double   price;
    double   quantity;
} Trade;

typedef struct {
    int64_t  row_index;
    double   open;
    double   high;
    double   low;
    double   close;
    double   volume;
    double   trade_count;
    double   vwap;
} Bar;

typedef struct {
    VenueId  venue;
    uint64_t order_id;
    double   filled_qty;
    double   avg_price;
    double   fees_paid;
    double   slippage_bps;
    int      latency_ms;
} FillResult;

typedef struct {
    uint64_t   parent_id;
    VenueId    venue;
    OrderSide  side;
    double     quantity;
    double     limit_price;   /* 0.0 = submit as MARKET                  */
    FillResult fill;
    bool       has_fill;
} ChildOrder;

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

static _Atomic uint64_t _or_id_counter = 1;
static inline uint64_t or_next_id(void) {
    return atomic_fetch_add_explicit(&_or_id_counter, 1,
                                     memory_order_relaxed);
}

typedef struct {
    double   total_filled_qty;
    double   avg_fill_price;      /* VWAP of all fills                   */
    double   total_fees_paid;
    double   slippage_bps;        /* vs reference price                  */
    double   fill_rate_pct;       /* filled / requested * 100            */
    int      n_trades;
    int      total_latency_ms;
} RouteResult;

#endif /* OR_TYPES_H */
