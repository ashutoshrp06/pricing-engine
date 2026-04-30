#ifndef EVENTS_H
#define EVENTS_H

#include "types.h"

struct LpQuote {
    LpId      lp_id;
    Price     bid;
    Price     ask;
    Timestamp production_timestamp;
};

struct SignalUpdate {
    Signal    value;
    Timestamp production_timestamp;
};

struct LtOrder {
    Side      side;
    Quantity  size;
    Timestamp production_timestamp;
};

#endif // EVENTS_H