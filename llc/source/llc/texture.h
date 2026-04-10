#pragma once

#include <cassert>
#include <span>

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <llc/context.h>
#include <llc/image.h>
#include <llc/types.hpp>

namespace llc {

Slang::ComPtr<rhi::ITexture> create_texture_2d(
    Context &context,
    u32 width,
    u32 height,
    rhi::Format format,
    rhi::TextureUsage usage,
    rhi::ResourceState default_state = rhi::ResourceState::UnorderedAccess);

Slang::ComPtr<rhi::ITexture> create_texture_2d(
    Context &context,
    const Image &image,
    u32 mip_count = 1,
    rhi::Format format = rhi::Format::Undefined,
    rhi::TextureUsage usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination,
    rhi::ResourceState default_state = rhi::ResourceState::ShaderResource);

Slang::ComPtr<rhi::ITexture> create_texture_2d(
    Context &context,
    std::span<const Image> mip_images,
    rhi::Format format = rhi::Format::Undefined,
    rhi::TextureUsage usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination,
    rhi::ResourceState default_state = rhi::ResourceState::ShaderResource);

struct TextureViewRange final {
    rhi::Format format = rhi::Format::Undefined;
    rhi::TextureAspect aspect = rhi::TextureAspect::All;
    u32 base_layer = 0;
    u32 layer_count = 1;
    u32 base_mip = 0;
    u32 mip_count = 1;
    rhi::ISampler *sampler = nullptr;
};

Slang::ComPtr<rhi::ITextureView> create_texture_view(
    Context &context,
    rhi::ITexture *texture,
    const TextureViewRange &range = {});

inline Slang::ComPtr<rhi::ITextureView> create_texture_view(
    Context &context,
    rhi::ITexture *texture,
    u32 mip_level,
    u32 array_layer = 0) {
    return create_texture_view(
        context,
        texture,
        TextureViewRange{
            .base_layer = array_layer,
            .layer_count = 1,
            .base_mip = mip_level,
            .mip_count = 1,
        });
}

Image read_texture_to_image(
    Context &context,
    rhi::ITexture *texture,
    u32 array_layer = 0,
    u32 mip_level = 0);

} // namespace llc
