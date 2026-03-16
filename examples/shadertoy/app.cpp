#include "app.h"

#include <filesystem>

#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>

#include <llc/image.h>
#include <llc/math.h>
#include <llc/texture.h>

namespace llc {

namespace {

constexpr char k_shader_module_name[] = "image";
constexpr char k_shader_entry_point[] = "main";
constexpr u32 k_image_width = 1024;
constexpr u32 k_image_height = 1024;
constexpr u32 k_group_size = 16;
constexpr char k_output_filename[] = "shadertoy.png";

} // namespace

i32 App::run(float time_seconds) {
    rhi::DeviceDesc device_desc;
    device_desc.slang.targetProfile = "spirv_1_6";
    device_desc.deviceType = rhi::DeviceType::Vulkan;

    device_ = rhi::getRHI()->createDevice(device_desc);
    if (!device_) {
        fmt::println("Failed to create Vulkan device.");
        return -1;
    }

    slang_session_ = device_->getSlangSession();
    slang_module_ = load_shader_module(slang_session_.get(), k_shader_module_name);
    if (!slang_module_) {
        fmt::println("Failed to load shader module: {}", k_shader_module_name);
        return -1;
    }

    shader_kernel_ = Kernel::load(slang_module_.get(), device_.get(), k_shader_entry_point);
    if (!shader_kernel_) {
        fmt::println("Failed to load compute kernel.");
        return -1;
    }

    auto output_texture = create_texture_2d(
        device_.get(),
        k_image_width,
        k_image_height,
        rhi::Format::RGBA8Unorm,
        rhi::TextureUsage::UnorderedAccess | rhi::TextureUsage::CopySource,
        rhi::ResourceState::UnorderedAccess);
    if (!output_texture) {
        fmt::println("Failed to create output texture.");
        return -1;
    }

    auto queue = device_->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto *pass = encoder->beginComputePass();
    {
        auto root_object = pass->bindPipeline(shader_kernel_.pipeline_.get());
        auto root_cursor = rhi::ShaderCursor(root_object);
        const u32 dims[2] = {k_image_width, k_image_height};
        auto output_view = create_texture_view(device_.get(), output_texture.get());
        SLANG_RETURN_ON_FAIL(root_cursor["dims"].setData(dims, sizeof(dims)));
        SLANG_RETURN_ON_FAIL(root_cursor["timeSeconds"].setData(&time_seconds, sizeof(time_seconds)));
        SLANG_RETURN_ON_FAIL(root_cursor["outputTexture"].setBinding(output_view));
        pass->dispatchCompute(
            divide_and_round_up(k_image_width, k_group_size),
            divide_and_round_up(k_image_height, k_group_size),
            1);
    }
    pass->end();

    Slang::ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    queue->waitOnHost();

    const auto image = read_texture_to_image(device_.get(), output_texture.get());
    if (!image) {
        fmt::println("Failed to read back output texture.");
        return -1;
    }

    const std::filesystem::path output_path = k_output_filename;
    if (!write_image_png(output_path, image)) {
        fmt::println("Failed to write PNG: {}", output_path.string());
        return -1;
    }

    fmt::println("Wrote {}", std::filesystem::absolute(output_path).string());
    return 0;
}

} // namespace llc
