#pragma once

#include <cstdint>
#include <cstddef>
#include <stdfloat>

namespace loia {

namespace types {

// Integer types
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Floating-point types

#if __STDCPP_FLOAT16_T__ == 1
using f16 = std::float16_t;
#endif

using f32 = float;
using f64 = double;

// Size types
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Pointer-sized integer (useful for some low-level operations)
#if defined(_WIN64) || defined(__x86_64__) || defined(__ppc64__) || defined(__aarch64__)
using iptr = std::int64_t;
using uptr = std::uint64_t;
#else
using iptr = std::int32_t;
using uptr = std::uint32_t;
#endif
} // namespace types

using namespace types;

} // namespace loia