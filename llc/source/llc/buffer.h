#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <span>

#include <llc/context.h>
#include <llc/types.hpp>

namespace llc {

Slang::ComPtr<rhi::IBuffer> create_structured_buffer(
    Context &context,
    u64 byte_size,
    u32 element_size,
    rhi::BufferUsage usage,
    const void *init_data = nullptr,
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess);

template <llc::standard_layout T>
Slang::ComPtr<rhi::IBuffer> create_structured_buffer(
    Context &context,
    rhi::BufferUsage usage,
    std::span<const T> init_data = {},
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess) {
    return create_structured_buffer(
        context,
        init_data.size_bytes(),
        sizeof(T),
        usage,
        init_data.data(),
        memory_type,
        rc_state);
}

/// Readonly view of data read back from GPU, owned the CPU copy of that data
template <llc::standard_layout T>
struct ReadbackView final {
    Slang::ComPtr<ISlangBlob> blob;

    // transparent, no ctors

    const T *data() const noexcept { return static_cast<const T *>(blob->getBufferPointer()); }
    usize size() const noexcept { return blob->getBufferSize() / sizeof(T); }

    // convert to std::span
    std::span<const T> as_span() const noexcept { return std::span<const T>(data(), size()); }

    // iterator support
    const T *begin() const noexcept { return data(); }
    const T *end() const noexcept { return data() + size(); }

    operator bool() const noexcept {
        return blob != nullptr && blob->getBufferPointer() != nullptr && blob->getBufferSize() > 0;
    }

    const T &operator[](usize index) const noexcept {
        assert(index < size());
        return data()[index];
    }
};

template <llc::standard_layout T>
ReadbackView<T> read_buffer(
    Context &context,
    rhi::IBuffer *buffer,
    rhi::Offset offset,
    u64 size) {

    Slang::ComPtr<ISlangBlob> blob;
    const rhi::Size byte_size = size * sizeof(T);
    if (SLANG_FAILED(context.device()->readBuffer(buffer, offset, byte_size, blob.writeRef()))) {
        assert(false && "Failed to read back buffer data from device.");
        return {};
    }
    return ReadbackView<T>{blob};
}

Slang::ComPtr<rhi::IBuffer> create_buffer(
    Context &context,
    u64 byte_size,
    rhi::BufferUsage usage,
    const void *init_data = nullptr,
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess);

template <llc::standard_layout T>
Slang::ComPtr<rhi::IBuffer> create_buffer(
    Context &context,
    rhi::BufferUsage usage,
    std::span<const T> init_data = {},
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess) {
    return create_buffer(
        context,
        init_data.size_bytes(),
        usage,
        init_data.data(),
        memory_type,
        rc_state);
}

template <llc::standard_layout T>
Slang::ComPtr<rhi::IBuffer> create_buffer(
    Context &context,
    u64 element_count,
    rhi::BufferUsage usage,
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess) {
    return create_buffer(
        context,
        element_count * sizeof(T),
        usage,
        nullptr,
        memory_type,
        rc_state);
}

/// clear buffer to all zeros, slang-rhi does not support clear with values currently
void clear_buffer(Context &context,
                  rhi::IBuffer *buffer,
                  rhi::BufferRange range = rhi::kEntireBuffer);

} // namespace llc
