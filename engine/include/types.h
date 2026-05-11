#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <limits>

using Price     = int64_t;
using Quantity  = int64_t;
using Timestamp = int64_t;
using LpId      = int32_t;   // 0 = PE, Non-Zero = LP IDs
using FillId    = int64_t;
using Signal    = double;    // SG: [-1.0, 1.0]

enum class Side : uint8_t {
    Buy,
    Sell
};

#endif // TYPES_H