#include "buffer.h"

namespace llc {

Slang::ComPtr<rhi::IBuffer> create_structured_buffer(
    Context &context,
    u64 byte_size,
    u32 element_size,
    rhi::BufferUsage usage,
    const void *init_data,
    rhi::MemoryType memory_type,
    rhi::ResourceState rc_state) {

    rhi::BufferDesc buffer_desc{
        .size = byte_size,
        .elementSize = element_size,
        .memoryType = memory_type,
        .usage = usage,
        .defaultState = rc_state,
    };

    return context.device()->createBuffer(buffer_desc, init_data);
}

Slang::ComPtr<rhi::IBuffer> create_buffer(
    Context &context,
    u64 byte_size,
    rhi::BufferUsage usage,
    const void *init_data,
    rhi::MemoryType memory_type,
    rhi::ResourceState rc_state) {

    rhi::BufferDesc buffer_desc{
        .size = byte_size,
        .memoryType = memory_type,
        .usage = usage,
        .defaultState = rc_state,
    };

    return context.device()->createBuffer(buffer_desc, init_data);
}

void clear_buffer(Context &context,
                  rhi::IBuffer *buffer,
                  rhi::BufferRange range) {
    auto queue = context.queue();
    auto encoder = queue->createCommandEncoder();
    encoder->clearBuffer(buffer, range);
    auto command_buffer = encoder->finish();
    queue->submit(command_buffer);
}

} // namespace llc
