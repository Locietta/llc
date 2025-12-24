#include "app.h"

#include <vector>

#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>

#include <llc/buffer.h>
#include <llc/math.h>

namespace llc {

using Slang::ComPtr;

i32 App::run(i32 argc, const char *argv[]) {
    rhi::DeviceDesc device_desc;
    device_desc.slang.targetProfile = "spirv_1_6";
    device_desc.deviceType = rhi::DeviceType::Vulkan;

    device_ = rhi::getRHI()->createDevice(device_desc);
    if (!device_) {
        fmt::println("Failed to create RHI device.");
        return -1;
    }
    slang_session_ = device_->getSlangSession();

    const char *kernel_name = "naive";
    slang_module_ = load_shader_module(slang_session_.get(), kernel_name);
    if (!slang_module_) {
        fmt::println("Failed to load shader module: {}", kernel_name);
        return -1;
    }

    naive_kernel_ = Kernel::load(slang_module_.get(), device_.get(), "main");
    if (!naive_kernel_) {
        fmt::println("Failed to load compute kernel.");
        return -1;
    }

    /// generate test data
    constexpr usize element_count = 256;
    constexpr usize thread_group_size = 256;

    std::vector<f32> init_data(element_count);
    {
        // fill from 1 to element_count
        for (usize i = 0; i < element_count; i++) {
            init_data[i] = static_cast<f32>(i + 1);
        }
    }
    const usize buffer_byte_size = sizeof(f32) * element_count;
    auto device_buffer = create_structured_buffer<f32>(
        device_.get(),
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
            rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess,
        init_data);

    if (!device_buffer) {
        fmt::println("Failed to create device buffer.");
        return -1;
    }

    auto queue = device_->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    if (!encoder) {
        fmt::println("Failed to create command encoder.");
        return -1;
    }

    /// dispatch the kernel
    {
        auto pass = encoder->beginComputePass();
        auto root_shader = pass->bindPipeline(naive_kernel_.pipeline_.get());
        auto root_cursor = rhi::ShaderCursor(root_shader);

        const auto bind_buffer = [&] (const char *name, rhi::BufferRange range) {
            return root_cursor[name].setBinding(rhi::Binding(device_buffer.get(), range));
        };

        SLANG_RETURN_ON_FAIL(bind_buffer("source", {0, buffer_byte_size}));
        SLANG_RETURN_ON_FAIL(bind_buffer("result", {0, buffer_byte_size}));

        const u32 group_count = divide_and_round_up(element_count, thread_group_size);
        pass->dispatchCompute(group_count, 1, 1);
        pass->end();
    }

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());

    queue->waitOnHost();
    ComPtr<ISlangBlob> blob;
    
    auto result_view = read_buffer<f32>(device_.get(), device_buffer.get(), 0, 1);
    if (!result_view) {
        fmt::println("Failed to read back buffer data from device.");
        return -1;
    }
    fmt::println("Reduction result: {}", result_view[0]);
    return 0;
}

} // namespace llc