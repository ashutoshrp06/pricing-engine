#ifndef ORDER_H
#define ORDER_H

#include "types.h"

struct Order {
    Price price;
    Quantity quantity;
    OrderId order_id;
    Timestamp production_timestamp;  // nanoseconds, used for end-to-end latency measurement
    LpId lp_id;                      // 0 = PE-owned (quote or hedge), >0 = LP id
    Side side;
    OrderIndex next = INVALID_INDEX, prev = INVALID_INDEX;
};

#endif