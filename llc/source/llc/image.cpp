#include "image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace llc {

namespace {

f32 srgb_to_linear(f32 value) noexcept {
    if (value <= 0.04045f) return value / 12.92f;
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

f32 linear_to_srgb(f32 value) noexcept {
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= 0.0031308f) return value * 12.92f;
    return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

u8 float_to_unorm8(f32 value) noexcept {
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<u8>(value * 255.0f + 0.5f);
}

f32 unorm8_to_float(u8 value) noexcept {
    return static_cast<f32>(value) / 255.0f;
}

bool is_rgba8_format(rhi::Format format) noexcept {
    return format == rhi::Format::RGBA8Unorm || format == rhi::Format::RGBA8UnormSrgb;
}

bool is_finite(f32 value) noexcept {
    return std::isfinite(value);
}

f32 sanitize_scalar(f32 value) noexcept {
    return is_finite(value) ? value : 0.0f;
}

void load_rgba_from_rgba8(const byte *src, rhi::Format format, f32 rgba[4]) noexcept {
    rgba[0] = unorm8_to_float(static_cast<u8>(src[0]));
    rgba[1] = unorm8_to_float(static_cast<u8>(src[1]));
    rgba[2] = unorm8_to_float(static_cast<u8>(src[2]));
    rgba[3] = unorm8_to_float(static_cast<u8>(src[3]));
    if (format == rhi::Format::RGBA8UnormSrgb) {
        rgba[0] = srgb_to_linear(rgba[0]);
        rgba[1] = srgb_to_linear(rgba[1]);
        rgba[2] = srgb_to_linear(rgba[2]);
    }
}

void store_rgba8_from_linear_rgba(byte *dst, rhi::Format format, const f32 rgba[4]) noexcept {
    const f32 alpha = sanitize_scalar(rgba[3]);
    if (format == rhi::Format::RGBA8UnormSrgb) {
        dst[0] = static_cast<byte>(float_to_unorm8(linear_to_srgb(sanitize_scalar(rgba[0]))));
        dst[1] = static_cast<byte>(float_to_unorm8(linear_to_srgb(sanitize_scalar(rgba[1]))));
        dst[2] = static_cast<byte>(float_to_unorm8(linear_to_srgb(sanitize_scalar(rgba[2]))));
    } else {
        dst[0] = static_cast<byte>(float_to_unorm8(sanitize_scalar(rgba[0])));
        dst[1] = static_cast<byte>(float_to_unorm8(sanitize_scalar(rgba[1])));
        dst[2] = static_cast<byte>(float_to_unorm8(sanitize_scalar(rgba[2])));
    }
    dst[3] = static_cast<byte>(float_to_unorm8(alpha));
}

} // namespace

Image::Image(u32 width, u32 height, rhi::Format format, usize row_pitch)
    : width(width), height(height), format(format), row_pitch(row_pitch), size_bytes(static_cast<usize>(height) * row_pitch), pixels(size_bytes > 0 ? std::make_unique<byte[]>(size_bytes) : nullptr) {}

usize bytes_per_pixel(rhi::Format format) noexcept {
    switch (format) {
        case rhi::Format::R8Unorm:
            return 1;
        case rhi::Format::RG8Unorm:
            return 2;
        case rhi::Format::RGBA8Unorm:
        case rhi::Format::RGBA8UnormSrgb:
            return 4;
        case rhi::Format::R32Float:
            return 4;
        case rhi::Format::RG32Float:
            return 8;
        case rhi::Format::RGBA32Float:
            return 16;
        default:
            return 0;
    }
}

Image convert_image(const Image &image, rhi::Format format) {
    if (!image) return {};
    if (format == rhi::Format::Undefined || image.format == format) {
        Image clone(image.width, image.height, image.format, image.row_pitch);
        std::memcpy(clone.data(), image.data(), image.size_bytes);
        return clone;
    }

    if (is_rgba8_format(image.format) && is_rgba8_format(format)) {
        Image converted(image.width, image.height, format, image.row_pitch);
        std::memcpy(converted.data(), image.data(), image.size_bytes);
        return converted;
    }

    const bool src_rgba8 = is_rgba8_format(image.format);
    const bool dst_rgba8 = is_rgba8_format(format);
    const bool src_r8 = image.format == rhi::Format::R8Unorm;
    const bool dst_r8 = format == rhi::Format::R8Unorm;
    const bool src_r32f = image.format == rhi::Format::R32Float;
    const bool dst_r32f = format == rhi::Format::R32Float;
    const bool src_rgba32f = image.format == rhi::Format::RGBA32Float;
    const bool dst_rgba32f = format == rhi::Format::RGBA32Float;
    const bool supported =
        (src_rgba8 && dst_rgba8) ||
        (src_rgba8 && dst_rgba32f) ||
        (src_rgba32f && dst_rgba8) ||
        (src_rgba32f && dst_rgba32f) ||
        (src_r8 && dst_r32f) ||
        (src_r32f && dst_r8) ||
        (src_r32f && dst_rgba8) ||
        (src_r32f && dst_rgba32f);
    if (!supported) {
        return {};
    }

    Image converted(
        image.width,
        image.height,
        format,
        static_cast<usize>(image.width) * bytes_per_pixel(format));

    for (u32 y = 0; y < image.height; ++y) {
        const auto *src_row = image.row_data(y);
        auto *dst_row = converted.row_data(y);
        for (u32 x = 0; x < image.width; ++x) {
            if (src_r8 && dst_r32f) {
                const auto *src = src_row + static_cast<usize>(x);
                auto *dst = reinterpret_cast<f32 *>(dst_row + static_cast<usize>(x) * sizeof(f32));
                dst[0] = unorm8_to_float(static_cast<u8>(src[0]));
                continue;
            }

            if (src_r32f && dst_r8) {
                const auto *src = reinterpret_cast<const f32 *>(src_row + static_cast<usize>(x) * sizeof(f32));
                auto *dst = dst_row + static_cast<usize>(x);
                dst[0] = static_cast<byte>(float_to_unorm8(sanitize_scalar(src[0])));
                continue;
            }

            if (src_r32f && dst_rgba8) {
                const auto *src = reinterpret_cast<const f32 *>(src_row + static_cast<usize>(x) * sizeof(f32));
                const f32 value = sanitize_scalar(src[0]);
                const f32 rgba[4] = {value, value, value, 1.0f};
                auto *dst = dst_row + static_cast<usize>(x) * 4;
                store_rgba8_from_linear_rgba(dst, format, rgba);
                continue;
            }

            if (src_r32f && dst_rgba32f) {
                const auto *src = reinterpret_cast<const f32 *>(src_row + static_cast<usize>(x) * sizeof(f32));
                const f32 value = sanitize_scalar(src[0]);
                auto *dst = reinterpret_cast<f32 *>(dst_row + static_cast<usize>(x) * 4 * sizeof(f32));
                dst[0] = value;
                dst[1] = value;
                dst[2] = value;
                dst[3] = 1.0f;
                continue;
            }

            f32 rgba[4]{};
            if (src_rgba8) {
                const auto *src = src_row + static_cast<usize>(x) * 4;
                load_rgba_from_rgba8(src, image.format, rgba);
            } else {
                const auto *src = reinterpret_cast<const f32 *>(src_row + static_cast<usize>(x) * 4 * sizeof(f32));
                rgba[0] = sanitize_scalar(src[0]);
                rgba[1] = sanitize_scalar(src[1]);
                rgba[2] = sanitize_scalar(src[2]);
                rgba[3] = sanitize_scalar(src[3]);
            }

            if (dst_rgba8) {
                auto *dst = dst_row + static_cast<usize>(x) * 4;
                store_rgba8_from_linear_rgba(dst, format, rgba);
            } else {
                auto *dst = reinterpret_cast<f32 *>(dst_row + static_cast<usize>(x) * 4 * sizeof(f32));
                dst[0] = rgba[0];
                dst[1] = rgba[1];
                dst[2] = rgba[2];
                dst[3] = rgba[3];
            }
        }
    }

    return converted;
}

bool write_image_png(const std::filesystem::path &path, const Image &image) {
    if (!image) return false;

    if (image.format == rhi::Format::R32Float) {
        auto converted = convert_image(image, rhi::Format::RGBA8Unorm);
        if (!converted) return false;
        return write_image_png(path, converted);
    }

    if (image.format != rhi::Format::RGBA8Unorm) return false;

    return stbi_write_png(
               path.string().c_str(),
               static_cast<int>(image.width),
               static_cast<int>(image.height),
               4,
               image.data(),
               static_cast<int>(image.row_pitch)) != 0;
}

} // namespace llc
