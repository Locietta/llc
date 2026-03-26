#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_int3.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/ext/vector_uint3.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <llc/f16.h>

namespace llc {

namespace types {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;

using byte = std::byte;

using f16x2 = glm::vec<2, f16, glm::defaultp>;
using f16x3 = glm::vec<3, f16, glm::defaultp>;
using f16x4 = glm::vec<4, f16, glm::defaultp>;

using f32x2 = glm::vec<2, f32, glm::defaultp>;
using f32x3 = glm::vec<3, f32, glm::defaultp>;
using f32x4 = glm::vec<4, f32, glm::defaultp>;

using f64x2 = glm::vec<2, f64, glm::defaultp>;
using f64x3 = glm::vec<3, f64, glm::defaultp>;
using f64x4 = glm::vec<4, f64, glm::defaultp>;

using i32x2 = glm::vec<2, i32, glm::defaultp>;
using i32x3 = glm::vec<3, i32, glm::defaultp>;
using i32x4 = glm::vec<4, i32, glm::defaultp>;

using u32x2 = glm::vec<2, u32, glm::defaultp>;
using u32x3 = glm::vec<3, u32, glm::defaultp>;
using u32x4 = glm::vec<4, u32, glm::defaultp>;

static_assert(sizeof(u8) == 1);
static_assert(sizeof(u16) == 2);
static_assert(sizeof(u32) == 4);
static_assert(sizeof(u64) == 8);
static_assert(sizeof(i8) == 1);
static_assert(sizeof(i16) == 2);
static_assert(sizeof(i32) == 4);
static_assert(sizeof(i64) == 8);
static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

} // namespace types

using namespace types;

/// traits

template <typename T>
concept standard_layout = std::is_standard_layout_v<T>;

} // namespace llc
