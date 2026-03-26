#pragma once

#include <compare>
#include <cstdint>
#include <limits>

namespace llc {

struct f16 { // NOLINT(readability-identifier-naming)
    using storage_type = std::uint16_t;

    constexpr f16() noexcept = default;
    constexpr f16(const f16 &) noexcept = default;
    constexpr f16 &operator=(const f16 &) noexcept = default;

    f16(float value) noexcept;

    static constexpr f16 from_bits(storage_type bits) noexcept { return f16(bits, Tag{}); }

    constexpr storage_type bits() const noexcept { return bits_; }

    operator float() const noexcept;

    constexpr f16 operator+() const noexcept { return *this; }
    f16 operator-() const noexcept;

    f16 &operator+=(f16 rhs) noexcept;
    f16 &operator-=(f16 rhs) noexcept;
    f16 &operator*=(f16 rhs) noexcept;
    f16 &operator/=(f16 rhs) noexcept;

    friend f16 operator+(f16 lhs, f16 rhs) noexcept {
        lhs += rhs;
        return lhs;
    }

    friend f16 operator-(f16 lhs, f16 rhs) noexcept {
        lhs -= rhs;
        return lhs;
    }

    friend f16 operator*(f16 lhs, f16 rhs) noexcept {
        lhs *= rhs;
        return lhs;
    }

    friend f16 operator/(f16 lhs, f16 rhs) noexcept {
        lhs /= rhs;
        return lhs;
    }

    constexpr bool operator==(const f16 &) const noexcept = default;
    auto operator<=>(const f16 &rhs) const noexcept {
        return static_cast<float>(*this) <=> static_cast<float>(rhs);
    }

private:
    struct Tag final {};
    constexpr f16(storage_type bits, Tag) noexcept : bits_(bits) {}
    storage_type bits_;
};

static_assert(sizeof(f16) == 2);

} // namespace llc

namespace std {

// This specialization mirrors std::numeric_limits naming from the standard.
// NOLINTBEGIN(readability-identifier-naming)
template <>
class numeric_limits<llc::f16> {
public:
    static constexpr bool is_specialized = true;
    static constexpr bool is_signed = true;
    static constexpr bool is_integer = false;
    static constexpr bool is_exact = false;
    static constexpr bool has_infinity = true;
    static constexpr bool has_quiet_NaN = true;
    static constexpr bool has_signaling_NaN = true;
    static constexpr float_denorm_style has_denorm = denorm_present;
    static constexpr bool has_denorm_loss = false;
    static constexpr float_round_style round_style = round_to_nearest;
    static constexpr bool is_iec559 = true;
    static constexpr bool is_bounded = true;
    static constexpr bool is_modulo = false;
    static constexpr int digits = 11;
    static constexpr int digits10 = 3;
    static constexpr int max_digits10 = 5;
    static constexpr int radix = 2;
    static constexpr int min_exponent = -13;
    static constexpr int min_exponent10 = -4;
    static constexpr int max_exponent = 16;
    static constexpr int max_exponent10 = 4;
    static constexpr bool traps = false;
    static constexpr bool tinyness_before = false;

    static constexpr llc::f16 min() noexcept { return llc::f16::from_bits(0x0400u); }
    static constexpr llc::f16 lowest() noexcept { return llc::f16::from_bits(0xfbffu); }
    static constexpr llc::f16 max() noexcept { return llc::f16::from_bits(0x7bffu); }
    static constexpr llc::f16 epsilon() noexcept { return llc::f16::from_bits(0x1400u); }
    static constexpr llc::f16 round_error() noexcept { return llc::f16::from_bits(0x3800u); }
    static constexpr llc::f16 infinity() noexcept { return llc::f16::from_bits(0x7c00u); }
    static constexpr llc::f16 quiet_NaN() noexcept { return llc::f16::from_bits(0x7e00u); }
    static constexpr llc::f16 signaling_NaN() noexcept { return llc::f16::from_bits(0x7d00u); }
    static constexpr llc::f16 denorm_min() noexcept { return llc::f16::from_bits(0x0001u); }
};
// NOLINTEND(readability-identifier-naming)

} // namespace std
