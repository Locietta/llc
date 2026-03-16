#pragma once

#include <cassert>
#include <filesystem>
#include <memory>

#include <slang-rhi.h>

#include <llc/types.hpp>

namespace llc {

struct Image final {
    u32 width = 0;
    u32 height = 0;
    rhi::Format format = rhi::Format::Undefined;
    usize row_pitch = 0;
    usize size_bytes = 0;
    std::unique_ptr<byte[]> pixels;

    Image() = default;
    Image(u32 width, u32 height, rhi::Format format, usize row_pitch);

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&) noexcept = default;
    Image &operator=(Image &&) noexcept = default;

    byte *data() noexcept { return pixels.get(); }
    const byte *data() const noexcept { return pixels.get(); }

    byte *row_data(u32 y) noexcept {
        assert(y < height);
        return data() + static_cast<usize>(y) * row_pitch;
    }

    const byte *row_data(u32 y) const noexcept {
        assert(y < height);
        return data() + static_cast<usize>(y) * row_pitch;
    }

    explicit operator bool() const noexcept {
        return pixels != nullptr && size_bytes > 0;
    }
};

usize bytes_per_pixel(rhi::Format format) noexcept;
Image convert_image(const Image &image, rhi::Format format);
bool write_image_png(const std::filesystem::path &path, const Image &image);

} // namespace llc
