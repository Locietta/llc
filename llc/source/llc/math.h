#pragma once

#include <concepts>

namespace llc {

// Note: this assumes `value+divisor` does not overflow
template <std::integral T>
constexpr T divide_and_round_up(T value, T divisor) noexcept {
    return (value + divisor - 1) / divisor;
}

} // namespace llc