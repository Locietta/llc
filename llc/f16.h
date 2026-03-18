#pragma once

#include <cstdint>

namespace llc {

struct f16 final { // NOLINT(readability-identifier-naming)
    using storage_type = std::uint16_t;

    constexpr f16() noexcept = default;
    constexpr f16(const f16 &) noexcept = default;
    constexpr f16 &operator=(const f16 &) noexcept = default;

    explicit f16(float value) noexcept;

    static constexpr f16 from_bits(storage_type bits) noexcept { return f16(bits, Tag{}); }

    constexpr storage_type bits() const noexcept { return bits_; }

    explicit operator float() const noexcept;

    constexpr bool operator==(const f16 &) const noexcept = default;

private:
    struct Tag final {};

    constexpr f16(storage_type bits, Tag) noexcept : bits_(bits) {}

    storage_type bits_ = 0;
};

static_assert(sizeof(f16) == sizeof(std::uint16_t));

} // namespace llc
