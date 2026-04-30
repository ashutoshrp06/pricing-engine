#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <limits>

using Price     = uint64_t;
using Quantity  = int64_t;
using OrderId   = uint64_t;
using Timestamp = int64_t;
using OrderIndex = uint32_t;
using LpId      = int32_t;   // 0 = PE, Non-Zero = LP IDs
using FillId    = int64_t;
using Signal    = double;    // SG: [-1.0, 1.0]

constexpr OrderIndex INVALID_INDEX = std::numeric_limits<OrderIndex>::max();

enum class Side : uint8_t {
    Buy,
    Sell
};

#endif // TYPES_H