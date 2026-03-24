#pragma once

#include <compare>
#include <cstdint>
#include <limits>

namespace llc {

struct f16_storage { // NOLINT(readability-identifier-naming)
    using storage_type = std::uint16_t;

    constexpr f16_storage() noexcept = default;
    constexpr f16_storage(const f16_storage &) noexcept = default;
    constexpr f16_storage &operator=(const f16_storage &) noexcept = default;

    f16_storage(float value) noexcept;

    static constexpr f16_storage from_bits(storage_type bits) noexcept { return f16_storage(bits, Tag{}); }

    constexpr storage_type bits() const noexcept { return bits_; }

    operator float() const noexcept;

    constexpr f16_storage operator+() const noexcept { return *this; }
    f16_storage operator-() const noexcept;

    f16_storage &operator+=(f16_storage rhs) noexcept;
    f16_storage &operator-=(f16_storage rhs) noexcept;
    f16_storage &operator*=(f16_storage rhs) noexcept;
    f16_storage &operator/=(f16_storage rhs) noexcept;

    friend f16_storage operator+(f16_storage lhs, f16_storage rhs) noexcept {
        lhs += rhs;
        return lhs;
    }

    friend f16_storage operator-(f16_storage lhs, f16_storage rhs) noexcept {
        lhs -= rhs;
        return lhs;
    }

    friend f16_storage operator*(f16_storage lhs, f16_storage rhs) noexcept {
        lhs *= rhs;
        return lhs;
    }

    friend f16_storage operator/(f16_storage lhs, f16_storage rhs) noexcept {
        lhs /= rhs;
        return lhs;
    }

    constexpr bool operator==(const f16_storage &) const noexcept = default;
    auto operator<=>(const f16_storage &rhs) const noexcept {
        return static_cast<float>(*this) <=> static_cast<float>(rhs);
    }

private:
    struct Tag final {};

    constexpr f16_storage(storage_type bits, Tag) noexcept : bits_(bits) {}

    storage_type bits_;
};

static_assert(sizeof(f16_storage) == 2);

struct alignas(4) f16 final : f16_storage { // NOLINT(readability-identifier-naming)
    using f16_storage::f16_storage;
    constexpr f16() noexcept = default;
    constexpr f16(const f16_storage &s) noexcept : f16_storage(s) {}
};

static_assert(sizeof(f16) == 4);

} // namespace llc

namespace std {

// This specialization mirrors std::numeric_limits naming from the standard.
// NOLINTBEGIN(readability-identifier-naming)
template <>
class numeric_limits<llc::f16_storage> {
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

    static constexpr llc::f16_storage min() noexcept { return llc::f16_storage::from_bits(0x0400u); }
    static constexpr llc::f16_storage lowest() noexcept { return llc::f16_storage::from_bits(0xfbffu); }
    static constexpr llc::f16_storage max() noexcept { return llc::f16_storage::from_bits(0x7bffu); }
    static constexpr llc::f16_storage epsilon() noexcept { return llc::f16_storage::from_bits(0x1400u); }
    static constexpr llc::f16_storage round_error() noexcept { return llc::f16_storage::from_bits(0x3800u); }
    static constexpr llc::f16_storage infinity() noexcept { return llc::f16_storage::from_bits(0x7c00u); }
    static constexpr llc::f16_storage quiet_NaN() noexcept { return llc::f16_storage::from_bits(0x7e00u); }
    static constexpr llc::f16_storage signaling_NaN() noexcept { return llc::f16_storage::from_bits(0x7d00u); }
    static constexpr llc::f16_storage denorm_min() noexcept { return llc::f16_storage::from_bits(0x0001u); }
};

template <>
class numeric_limits<llc::f16> : public numeric_limits<llc::f16_storage> {};
// NOLINTEND(readability-identifier-naming)

} // namespace std
