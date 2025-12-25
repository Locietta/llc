#include "app.h"

#include <vector>

#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>

#include <llc/buffer.h>
#include <llc/math.h>
#include <llc/timer.h>

namespace llc {

using Slang::ComPtr;

constexpr u32 calc_reduce_times(u32 length, u32 group_size) noexcept {
    if (length <= 1) return 0;

    const u32 a_msb_pos = 32u - std::countl_zero(length - 1);
    const u32 b_msb_pos = 32u - std::countl_zero(group_size - 1);

    return divide_and_round_up(a_msb_pos, b_msb_pos);
}

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
    constexpr u32 element_count = 1 << 25;
    constexpr u32 thread_group_size = 256;

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

    const auto reduce_times = calc_reduce_times(element_count, thread_group_size);
    GpuTimer gpu_timer(device_.get(), reduce_times * 2);
    GpuTimer::Frame timer_frame(gpu_timer);
    if (!gpu_timer) {
        fmt::println("Warning: GPU timer is not available.");
    }

    /// dispatch the kernelss
    for (u32 i = 0, l = element_count; i < reduce_times; i++) {
        const auto time_scope = gpu_timer.scope(encoder);

        auto pass = encoder->beginComputePass();
        {
            auto root_shader = pass->bindPipeline(naive_kernel_.pipeline_.get());
            auto root_cursor = rhi::ShaderCursor(root_shader);

            const auto bind_buffer = [&](const char *name, rhi::BufferRange range) {
                return root_cursor[name].setBinding(rhi::Binding(device_buffer.get(), range));
            };
            const u32 group_count = divide_and_round_up(l, thread_group_size);
            const u32 input_byte_size = sizeof(f32) * l;
            const u32 output_byte_size = sizeof(f32) * group_count;
            l = group_count;

            SLANG_RETURN_ON_FAIL(bind_buffer("source", {0, input_byte_size}));
            SLANG_RETURN_ON_FAIL(bind_buffer("result", {0, output_byte_size}));
            pass->dispatchCompute(group_count, 1, 1);
        }
        pass->end();
    }

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());

    queue->waitOnHost();
    ComPtr<ISlangBlob> blob;

    if (timer_frame.resolve()) {
        auto pass_durations = timer_frame.pair_durations();
        if (!pass_durations.empty()) {
            double total_gpu_time_sec = 0.0;
            fmt::println(
                "GPU timing ({} passes, freq {} Hz):",
                reduce_times,
                gpu_timer.timestamp_frequency());
            for (usize i = 0; i < pass_durations.size(); ++i) {
                total_gpu_time_sec += pass_durations[i];
                fmt::println("  Pass {}: {:.3f} us", i, pass_durations[i] * 1e6);
            }
            fmt::println("Total GPU time: {:.3f} us", total_gpu_time_sec * 1e6);
        }
    }

    auto result_view = read_buffer<f32>(device_.get(), device_buffer.get(), 0, 1);
    if (!result_view) {
        fmt::println("Failed to read back buffer data from device.");
        return -1;
    }
    fmt::println("Reduction result: {}", result_view[0]);
    return 0;
}

} // namespace llc