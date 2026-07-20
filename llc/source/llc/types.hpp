#pragma once

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

#include <llc/scalar_types.hpp>
#include <llc/f16.h>

namespace llc {

namespace types {

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
static_assert(sizeof(f32x2) == sizeof(f32) * 2);
static_assert(sizeof(f32x3) == sizeof(f32) * 3);
static_assert(sizeof(f32x4) == sizeof(f32) * 4);
static_assert(sizeof(i32x2) == sizeof(i32) * 2);
static_assert(sizeof(i32x3) == sizeof(i32) * 3);
static_assert(sizeof(i32x4) == sizeof(i32) * 4);
static_assert(sizeof(u32x2) == sizeof(u32) * 2);
static_assert(sizeof(u32x3) == sizeof(u32) * 3);
static_assert(sizeof(u32x4) == sizeof(u32) * 4);

} // namespace types

using namespace types;

/// traits

template <typename T>
concept standard_layout = std::is_standard_layout_v<T>;

} // namespace llc
