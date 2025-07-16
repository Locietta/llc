#pragma once

#include <bit>
#include <concepts>
#include <limits>

namespace loia {

/// pre: x > 0
template <std::unsigned_integral T>
constexpr T log2_ceil(T x) {
    return std::numeric_limits<T>::digits - std::countl_zero(x - 1);
}

} // namespace loia