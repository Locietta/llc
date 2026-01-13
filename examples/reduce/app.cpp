#include "app.h"

#include <vector>

#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>
#include <cxxopts.hpp>

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
    cxxopts::Options options("reduce", "Reduce an array of floats using GPU compute shader");
    options.add_options()                                                                                     //
        ("kernel", "Kernel to use [naive|wave]", cxxopts::value<std::string>()->default_value("naive"))       //
        ("backend", "RHI backend to use [dx|vk|auto]", cxxopts::value<std::string>()->default_value("auto")); //

    options.parse_positional({"kernel", "backend"});
    auto result = options.parse(argc, argv);

    const std::string kernel_name = result["kernel"].as<std::string>();
    const std::string backend_name = result["backend"].as<std::string>();

    rhi::DeviceDesc device_desc;
    if (backend_name == "auto") {
        device_desc.deviceType = rhi::DeviceType::Default;
    } else if (backend_name == "vk") {
        device_desc.slang.targetProfile = "spirv_1_6";
        device_desc.deviceType = rhi::DeviceType::Vulkan;
    } else if (backend_name == "dx") {
        device_desc.slang.targetProfile = "sm_6_6";
        device_desc.deviceType = rhi::DeviceType::D3D12;
    } else {
        fmt::println("Unsupported backend: {}", backend_name);
        return -1;
    }

    device_ = rhi::getRHI()->createDevice(device_desc);
    if (!device_) {
        fmt::println("Failed to create RHI device.");
        return -1;
    }
    slang_session_ = device_->getSlangSession();

    slang_module_ = load_shader_module(slang_session_.get(), kernel_name.c_str());
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
    const u32 thread_group_size = kernel_name == "wave" ? 512 : 256;

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
    auto gpu_timer = GpuTimer::create(device_.get(), reduce_times);

    if (!gpu_timer) {
        fmt::println("Warning: GPU timer is not available.");
    }

    /// dispatch the kernelss
    for (u32 i = 0, l = element_count; i < reduce_times; i++) {
        const auto timer_scope = gpu_timer ?
                                     gpu_timer->scope(encoder.get(), fmt::format("reduce pass {:02}", i)) :
                                     GpuTimer::Scope{};

        auto *pass = encoder->beginComputePass();
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

    if (gpu_timer && gpu_timer->resolve()) {
        const auto labeled_durations = gpu_timer->labeled_durations();

        fmt::println(
            "GPU timing ({} passes, freq {} Hz):",
            labeled_durations.size(),
            gpu_timer->timestamp_frequency());

        f64 total_gpu_time_sec = 0.0;
        for (const auto &[label, duration] : labeled_durations) {
            total_gpu_time_sec += duration;
            fmt::println("    [{}] {:.3f} us", label, duration * 1e6);
        }
        fmt::println("Total GPU time: {:.3f} us", total_gpu_time_sec * 1e6);
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