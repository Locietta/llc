#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <llc/types.hpp>

namespace llc::pp {

template <typename T>
usize reduce_sum_scratch_size(usize count);

template <typename T>
SlangResult encode_reduce_sum(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::IBuffer *source,
    usize count,
    rhi::IBuffer *result);

template <typename T>
T reduce_sum(rhi::IDevice *device, rhi::IBuffer *source, usize count);

} // namespace llc::pp
