#include "texture.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <slang-rhi/shader-cursor.h>

#include <llc/blob.h>
#include <llc/types.hpp>

#include <llc/utils/embedded_module.h>
#include <llc/utils/pipeline_cache.h>

namespace llc {

namespace {

extern "C" const unsigned char _binary_generate_mips_slang_module_start[]; // NOLINT
extern "C" const unsigned char _binary_generate_mips_slang_module_end[];   // NOLINT

} // namespace

u32 compute_max_mip_count(u32 width, u32 height) noexcept {
    u32 mip_count = 1;
    u32 size = std::max(width, height);
    while (size > 1) {
        size >>= 1;
        ++mip_count;
    }
    return mip_count;
}

bool supports_auto_mip_generation(rhi::Format format) noexcept {
    return format == rhi::Format::RGBA8Unorm || format == rhi::Format::RGBA32Float;
}

std::string mip_format_specialization_expr(rhi::Format format) {
    switch (format) {
        case rhi::Format::RGBA8Unorm:
            return std::to_string(static_cast<int>(SLANG_IMAGE_FORMAT_rgba8));
        case rhi::Format::RGBA32Float:
            return std::to_string(static_cast<int>(SLANG_IMAGE_FORMAT_rgba32f));
        default:
            return {};
    }
}

Slang::ComPtr<rhi::IComputePipeline> create_generate_mips_pipeline(rhi::IDevice *device, rhi::Format format) {
    auto module = load_embedded_module(device, EmbededModuleDesc{
                                                   .name = "generate_mips",
                                                   .start = _binary_generate_mips_slang_module_start,
                                                   .end = _binary_generate_mips_slang_module_end,
                                               });
    if (!module) return nullptr;

    Slang::ComPtr<slang::IEntryPoint> entry_point;
    if (SLANG_FAILED(module->findEntryPointByName("mainCompute", entry_point.writeRef()))) {
        return nullptr;
    }

    const auto specialization_expr = mip_format_specialization_expr(format);
    if (specialization_expr.empty()) return nullptr;

    slang::SpecializationArg specialization_arg = slang::SpecializationArg::fromExpr(specialization_expr.c_str());
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IComponentType> specialized_entry_point;
    if (SLANG_FAILED(entry_point->specialize(
            &specialization_arg,
            1,
            specialized_entry_point.writeRef(),
            diagnostics.writeRef()))) {
        diagnose_if_needed(diagnostics.get());
        return nullptr;
    }
    diagnose_if_needed(diagnostics.get());

    Slang::ComPtr<slang::IComponentType> linked_program;
    diagnostics = nullptr;
    if (SLANG_FAILED(specialized_entry_point->link(linked_program.writeRef(), diagnostics.writeRef()))) {
        diagnose_if_needed(diagnostics.get());
        return nullptr;
    }
    diagnose_if_needed(diagnostics.get());

    auto program = device->createShaderProgram(linked_program);
    if (!program) return nullptr;

    rhi::ComputePipelineDesc desc{};
    desc.program = program.get();
    return device->createComputePipeline(desc);
}

bool validate_mip_image_chain(std::span<const Image> mip_images, rhi::Format format) noexcept {
    if (mip_images.empty()) return false;
    const auto base_width = mip_images[0].width;
    const auto base_height = mip_images[0].height;
    for (usize i = 0; i < mip_images.size(); ++i) {
        const auto expected_width = std::max(1u, base_width >> static_cast<u32>(i));
        const auto expected_height = std::max(1u, base_height >> static_cast<u32>(i));
        const auto &image = mip_images[i];
        if (!image || image.format != format || image.width != expected_width || image.height != expected_height) {
            return false;
        }
    }
    return true;
}

bool upload_mip_images(
    rhi::IDevice *device,
    rhi::ITexture *texture,
    std::span<const Image> mip_images) {

    auto queue = device->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    for (u32 mip = 0; mip < mip_images.size(); ++mip) {
        const auto &image = mip_images[mip];
        const rhi::SubresourceData data{
            .data = image.data(),
            .rowPitch = image.row_pitch,
            .slicePitch = image.size_bytes,
        };
        if (SLANG_FAILED(encoder->uploadTextureData(
                texture,
                rhi::SubresourceRange{0, 1, mip, 1},
                {},
                rhi::Extent3D{image.width, image.height, 1},
                &data,
                1))) {
            return false;
        }
    }

    auto command_buffer = encoder->finish();
    queue->submit(command_buffer);
    queue->waitOnHost();
    return true;
}

bool generate_texture_mips(rhi::IDevice *device, rhi::ITexture *texture) {
    const auto &desc = texture->getDesc();
    if (desc.mipCount <= 1) return true;

    /// generate pipeline key for cache
    const std::string pipeline_key = "generate_mips_" + [&desc] {
        switch (desc.format) {
            case rhi::Format::RGBA8Unorm:
                return std::string("rgba8");
            case rhi::Format::RGBA32Float:
                return std::string("rgba32f");
            default:
                return std::string{};
        }
    }();

    auto pipeline = get_cached_pipeline(g_buffer_pipelines, device, pipeline_key,
                                        [&desc](rhi::IDevice *d) {
                                            return create_generate_mips_pipeline(d, desc.format);
                                        });
    if (!pipeline) return false;

    auto queue = device->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    for (u32 mip = 1; mip < desc.mipCount; ++mip) {
        const auto src_width = std::max(1u, desc.size.width >> (mip - 1));
        const auto src_height = std::max(1u, desc.size.height >> (mip - 1));
        const auto dst_width = std::max(1u, desc.size.width >> mip);
        const auto dst_height = std::max(1u, desc.size.height >> mip);

        auto src_view = create_texture_view(device, texture, mip - 1);
        auto dst_view = create_texture_view(device, texture, mip);
        auto *pass = encoder->beginComputePass();
        {
            auto root_object = pass->bindPipeline(pipeline.get());
            auto cursor = rhi::ShaderCursor(root_object);
            const u32 src_size[2] = {src_width, src_height};
            const u32 dst_size[2] = {dst_width, dst_height};
            if (SLANG_FAILED(cursor["srcSize"].setData(src_size, sizeof(src_size))) ||
                SLANG_FAILED(cursor["dstSize"].setData(dst_size, sizeof(dst_size))) ||
                SLANG_FAILED(cursor["src"].setBinding(src_view)) ||
                SLANG_FAILED(cursor["dst"].setBinding(dst_view))) {
                return false;
            }
            pass->dispatchCompute((dst_width + 15) / 16, (dst_height + 15) / 16, 1);
        }
        pass->end();
    }

    auto command_buffer = encoder->finish();
    queue->submit(command_buffer);
    queue->waitOnHost();
    return true;
}

Slang::ComPtr<rhi::ITexture> create_texture_2d(
    rhi::IDevice *device,
    u32 width,
    u32 height,
    rhi::Format format,
    rhi::TextureUsage usage,
    rhi::ResourceState default_state) {

    rhi::TextureDesc desc{};
    desc.type = rhi::TextureType::Texture2D;
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depth = 1;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.format = format;
    desc.usage = usage;
    desc.defaultState = default_state;
    return device->createTexture(desc);
}

Slang::ComPtr<rhi::ITexture> create_texture_2d(
    rhi::IDevice *device,
    const Image &image,
    u32 mip_count,
    rhi::Format format,
    rhi::TextureUsage usage,
    rhi::ResourceState default_state) {

    if (!image || mip_count == 0) return nullptr;

    const auto target_format = format == rhi::Format::Undefined ? image.format : format;
    auto converted_image = convert_image(image, target_format);
    if (!converted_image) return nullptr;

    const auto max_mip_count = compute_max_mip_count(converted_image.width, converted_image.height);
    if (mip_count > max_mip_count) return nullptr;

    const auto auto_generate_mips = mip_count > 1;
    if (auto_generate_mips && !supports_auto_mip_generation(target_format)) {
        return nullptr;
    }

    auto texture_usage = usage;
    if (auto_generate_mips) {
        texture_usage = texture_usage | rhi::TextureUsage::UnorderedAccess;
    }

    rhi::TextureDesc desc{};
    desc.type = rhi::TextureType::Texture2D;
    desc.size.width = converted_image.width;
    desc.size.height = converted_image.height;
    desc.size.depth = 1;
    desc.mipCount = mip_count;
    desc.arrayLength = 1;
    desc.format = target_format;
    desc.usage = texture_usage;
    desc.defaultState = default_state;

    auto texture = device->createTexture(desc);
    if (!texture) return nullptr;
    if (!upload_mip_images(device, texture.get(), std::span<const Image>(&converted_image, 1))) {
        return nullptr;
    }
    if (auto_generate_mips && !generate_texture_mips(device, texture.get())) {
        return nullptr;
    }
    return texture;
}

Slang::ComPtr<rhi::ITexture> create_texture_2d(
    rhi::IDevice *device,
    std::span<const Image> mip_images,
    rhi::Format format,
    rhi::TextureUsage usage,
    rhi::ResourceState default_state) {

    if (mip_images.empty()) return nullptr;

    const auto target_format = format == rhi::Format::Undefined ? mip_images[0].format : format;
    std::vector<Image> converted_images;
    converted_images.reserve(mip_images.size());
    for (const auto &image : mip_images) {
        auto converted = convert_image(image, target_format);
        if (!converted) return nullptr;
        converted_images.push_back(std::move(converted));
    }

    const std::span<const Image> converted_span(converted_images.data(), converted_images.size());
    if (!validate_mip_image_chain(converted_span, target_format)) return nullptr;

    rhi::TextureDesc desc{};
    desc.type = rhi::TextureType::Texture2D;
    desc.size.width = converted_images[0].width;
    desc.size.height = converted_images[0].height;
    desc.size.depth = 1;
    desc.mipCount = static_cast<u32>(converted_images.size());
    desc.arrayLength = 1;
    desc.format = target_format;
    desc.usage = usage;
    desc.defaultState = default_state;

    auto texture = device->createTexture(desc);
    if (!texture) return nullptr;
    if (!upload_mip_images(device, texture.get(), converted_span)) {
        return nullptr;
    }
    return texture;
}

Slang::ComPtr<rhi::ITextureView> create_texture_view(
    rhi::IDevice *device,
    rhi::ITexture *texture,
    const TextureViewRange &range) {

    rhi::TextureViewDesc desc{};
    desc.format = range.format;
    desc.aspect = range.aspect;
    desc.subresourceRange = {
        .layer = range.base_layer,
        .layerCount = range.layer_count,
        .mip = range.base_mip,
        .mipCount = range.mip_count,
    };
    desc.sampler = range.sampler;
    return device->createTextureView(texture, desc);
}

Image read_texture_to_image(
    rhi::IDevice *device,
    rhi::ITexture *texture,
    u32 array_layer,
    u32 mip_level) {

    Slang::ComPtr<ISlangBlob> blob;
    rhi::SubresourceLayout layout{};
    if (SLANG_FAILED(device->readTexture(texture, array_layer, mip_level, blob.writeRef(), &layout))) {
        assert(false && "Failed to read back texture data from device.");
        return {};
    }

    const auto &desc = texture->getDesc();
    const u32 mip_width = desc.size.width >> mip_level;
    const u32 mip_height = desc.size.height >> mip_level;
    const u32 width = mip_width > 0 ? mip_width : 1u;
    const u32 height = mip_height > 0 ? mip_height : 1u;
    const usize pixel_stride = bytes_per_pixel(desc.format);
    if (pixel_stride == 0) {
        assert(false && "Unsupported texture format for Image readback.");
        return {};
    }

    Image image(width, height, desc.format, static_cast<usize>(width) * pixel_stride);
    for (u32 y = 0; y < height; ++y) {
        const auto *src =
            static_cast<const byte *>(blob->getBufferPointer()) + static_cast<usize>(y) * layout.rowPitch;
        std::memcpy(image.row_data(y), src, image.row_pitch);
    }
    return image;
}

} // namespace llc
