#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <llc/types.hpp>

namespace llc::pp {

u64 reduce_sum_scratch_size_f32(u32 count);

SlangResult encode_reduce_sum_f32(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::IBuffer *source,
    u32 count,
    rhi::IBuffer *scratch,
    u64 scratch_offset,
    rhi::IBuffer *result);

f32 reduce_sum_f32(rhi::IDevice *device, rhi::IBuffer *source, u32 count);

u64 reduce_sum_texture_r32f_scratch_size(u32 width, u32 height);

SlangResult encode_reduce_sum_texture_r32f(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::ITexture *source,
    u32 width,
    u32 height,
    rhi::IBuffer *scratch,
    u64 scratch_offset,
    rhi::IBuffer *result);

f32 reduce_sum_texture_r32f(rhi::IDevice *device, rhi::ITexture *source, u32 width, u32 height);

} // namespace llc::pp
