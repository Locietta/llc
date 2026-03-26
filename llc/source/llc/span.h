#pragma once

#include <cassert>

#include <slang-rhi.h>

#include <llc/types.hpp>

namespace llc {

/// GPU-side span descriptor: {device_address, element_count}.
/// Layout matches the Slang Span<T>/MutSpan<T> structs (16 bytes).
struct GpuSpan final {
    u64 address = 0;
    u64 count = 0;
};

static_assert(sizeof(GpuSpan) == 16);
static_assert(alignof(GpuSpan) == 8);

/// Create a GpuSpan from a buffer and element count.
template <standard_layout T>
GpuSpan make_span(rhi::IBuffer *buffer, u64 element_count) {
    assert(buffer);
    return GpuSpan{
        .address = buffer->getDeviceAddress(),
        .count = element_count,
    };
}

/// Create a GpuSpan covering the entire buffer, inferring count from byte size.
template <standard_layout T>
GpuSpan make_span(rhi::IBuffer *buffer) {
    assert(buffer);
    return GpuSpan{
        .address = buffer->getDeviceAddress(),
        .count = buffer->getDesc().size / sizeof(T),
    };
}

/// Create a GpuSpan at a byte offset into a buffer.
template <standard_layout T>
GpuSpan make_span(rhi::IBuffer *buffer, u64 byte_offset, u64 element_count) {
    assert(buffer);
    return GpuSpan{
        .address = buffer->getDeviceAddress() + byte_offset,
        .count = element_count,
    };
}

Slang::ComPtr<slang::IModule> load_span_module(slang::ISession *session);
Slang::ComPtr<slang::IModule> load_span_module(rhi::IDevice *device);

} // namespace llc
