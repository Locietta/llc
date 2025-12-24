#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <span>

#include <llc/types.hpp>

namespace llc {

Slang::ComPtr<rhi::IBuffer> create_structured_buffer(
    rhi::IDevice *device,
    u64 byte_size,
    u32 element_size,
    rhi::BufferUsage usage,
    const void *init_data = nullptr,
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess);

template <llc::standard_layout T>
Slang::ComPtr<rhi::IBuffer> create_structured_buffer(
    rhi::IDevice *device,
    rhi::BufferUsage usage,
    std::span<const T> init_data = {},
    rhi::MemoryType memory_type = rhi::MemoryType::DeviceLocal,
    rhi::ResourceState rc_state = rhi::ResourceState::UnorderedAccess) {
    return create_structured_buffer(
        device,
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
    rhi::IDevice *device,
    rhi::IBuffer *buffer,
    rhi::Offset offset,
    u64 size) {

    Slang::ComPtr<ISlangBlob> blob;
    const rhi::Size byte_size = size * sizeof(T);
    if (SLANG_FAILED(device->readBuffer(buffer, offset, byte_size, blob.writeRef()))) {
        assert(false && "Failed to read back buffer data from device.");
        return {};
    }
    return ReadbackView<T>{blob};
}

/// clear buffer to all zeros, slang-rhi does not support clear with values currently
void clear_buffer(rhi::IDevice *device,
                  rhi::IBuffer *buffer,
                  rhi::BufferRange range = rhi::kEntireBuffer);

} // namespace llc