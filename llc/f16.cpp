#include "f16.h"

#include <bit>
#include <llc/types.hpp>

namespace llc {

namespace {

using storage_type = f16_storage::storage_type;

storage_type float_to_f16_bits(float value) noexcept {
    const u32 bits = std::bit_cast<u32>(value);
    storage_type result = static_cast<storage_type>((bits >> 16) & 0x8000u);
    storage_type mantissa = static_cast<storage_type>((bits >> 12) & 0x07ffu);
    const u32 exponent = (bits >> 23) & 0xffu;

    if (exponent < 103) return result;
    if (exponent > 142) {
        result = static_cast<storage_type>(result | 0x7c00u);
        result = static_cast<storage_type>(result | (exponent == 255 && (bits & 0x007fffffu)));
        return result;
    }
    if (exponent < 113) {
        mantissa = static_cast<storage_type>(mantissa | 0x0800u);
        result = static_cast<storage_type>(result | (mantissa >> (114 - exponent)));
        result = static_cast<storage_type>(result + ((mantissa >> (113 - exponent)) & 1u));
        return result;
    }

    result = static_cast<storage_type>(result | ((exponent - 112) << 10) | (mantissa >> 1));
    result = static_cast<storage_type>(result + (mantissa & 1u));
    return result;
}

float f16_bits_to_float(storage_type bits) noexcept {
    const u32 sign = (static_cast<u32>(bits & 0x8000u)) << 16;
    u32 exponent = (bits >> 10) & 0x1fu;
    u32 mantissa = bits & 0x03ffu;

    if (exponent == 0) {
        if (mantissa == 0) {
            return std::bit_cast<float>(sign);
        }
        exponent = 113;
        while ((mantissa & 0x0400u) == 0) {
            mantissa <<= 1;
            --exponent;
        }
        mantissa &= 0x03ffu;
        const u32 value_bits = sign | (exponent << 23) | (mantissa << 13);
        return std::bit_cast<float>(value_bits);
    }

    if (exponent == 31) {
        const u32 value_bits = sign | 0x7f800000u | (mantissa << 13);
        return std::bit_cast<float>(value_bits);
    }

    exponent = exponent + (127 - 15);
    const u32 value_bits = sign | (exponent << 23) | (mantissa << 13);
    return std::bit_cast<float>(value_bits);
}

} // namespace

f16_storage::f16_storage(float value) noexcept : bits_(float_to_f16_bits(value)) {}

f16_storage::operator float() const noexcept {
    return f16_bits_to_float(bits_);
}

f16_storage f16_storage::operator-() const noexcept {
    return from_bits(static_cast<storage_type>(bits_ ^ 0x8000u));
}

f16_storage &f16_storage::operator+=(f16_storage rhs) noexcept {
    *this = f16_storage(static_cast<float>(*this) + static_cast<float>(rhs));
    return *this;
}

f16_storage &f16_storage::operator-=(f16_storage rhs) noexcept {
    *this = f16_storage(static_cast<float>(*this) - static_cast<float>(rhs));
    return *this;
}

f16_storage &f16_storage::operator*=(f16_storage rhs) noexcept {
    *this = f16_storage(static_cast<float>(*this) * static_cast<float>(rhs));
    return *this;
}

f16_storage &f16_storage::operator/=(f16_storage rhs) noexcept {
    *this = f16_storage(static_cast<float>(*this) / static_cast<float>(rhs));
    return *this;
}

} // namespace llc
