#include "app.h"

#include <vector>

#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>
#include <cxxopts.hpp>

#include <llc/kernel.h>
#include <llc/buffer.h>
#include <llc/math.h>
#include <llc/pp/reduce.h>
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
        ("backend", "RHI backend to use [dx|vk|auto]", cxxopts::value<std::string>()->default_value("auto")); //

    options.parse_positional({"backend"});
    auto result = options.parse(argc, argv);

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

    auto context = Context::create(ContextDesc{.device = device_desc});
    if (!context) {
        fmt::println("Failed to create RHI device.");
        return -1;
    }
    context_ = std::move(*context);

    constexpr u32 element_count = 1 << 25;

    std::vector<f32> init_data(element_count);
    {
        // fill from 1 to element_count
        for (usize i = 0; i < element_count; i++) {
            init_data[i] = static_cast<f32>(i + 1);
        }
    }
    const f64 cpu_result = static_cast<f64>(element_count) * static_cast<f64>(element_count + 1) * 0.5;

    for (const auto module_name : {"naive", "wave"}) {
        fmt::println("Running reduce with module: {}", module_name);
        auto module = load_shader_module(context_, module_name);
        if (!module) {
            fmt::println("Failed to load shader module: {}", module_name);
            return -1;
        }

        auto kernel = Kernel::load(module.get(), context_, "main");
        if (!kernel) {
            fmt::println("Failed to load kernel from module.");
            return -1;
        }

        const u32 thread_group_size = strcmp(module_name, "naive") == 0 ? 256 : 512;
        const usize buffer_byte_size = sizeof(f32) * element_count;

        auto device_buffer = create_structured_buffer<f32>(
            context_,
            rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess,
            init_data);

        if (!device_buffer) {
            fmt::println("Failed to create device buffer.");
            return -1;
        }

        auto queue = context_.queue();
        auto encoder = queue->createCommandEncoder();
        if (!encoder) {
            fmt::println("Failed to create command encoder.");
            return -1;
        }

        const auto reduce_times = calc_reduce_times(element_count, thread_group_size);
        auto gpu_timer = GpuTimer::create(context_, reduce_times);

        if (!gpu_timer) {
            fmt::println("Warning: GPU timer is not available.");
        }

        for (u32 i = 0, l = element_count; i < reduce_times; i++) {
            const auto timer_scope = gpu_timer ?
                                         gpu_timer->scope(encoder.get(), fmt::format("reduce pass {:02}", i)) :
                                         GpuTimer::Scope{};

            auto *pass = encoder->beginComputePass();
            {
                auto root_shader = pass->bindPipeline(kernel.pipeline_.get());
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

        if (gpu_timer && gpu_timer->resolve()) {
            const auto labeled_durations = gpu_timer->labeled_durations();
            const auto timestamps = gpu_timer->raw_timestamps();

            for (const auto &[label, duration] : labeled_durations) {
                fmt::println("    [{}] {:.3f} us", label, duration * 1e6);
            }
            if (timestamps.size() >= 2) {
                const f64 total = gpu_timer->ticks_to_seconds(timestamps.back() - timestamps.front());
                fmt::println("Total GPU time: {:.3f} us", total * 1e6);
            }
        }

        auto result_view = read_buffer<f32>(context_, device_buffer.get(), 0, 1);
        if (!result_view) {
            fmt::println("Failed to read back buffer data from device.");
            return -1;
        }
        const f32 gpu_result = result_view[0];
        fmt::println("reduction({}) result: {}", module_name, gpu_result);
        fmt::println("abs error: {}", std::abs(static_cast<f64>(gpu_result) - cpu_result));
        fmt::println("===============================");
    }

    // Ping-pong verification: use two separate buffers to confirm single-buffer aliasing is safe
    {
        fmt::println("Running reduce with module: wave-pingpong");
        auto module = load_shader_module(context_, "wave");
        if (!module) {
            fmt::println("Failed to load shader module: wave");
            return -1;
        }

        auto kernel = Kernel::load(module.get(), context_, "main");
        if (!kernel) {
            fmt::println("Failed to load kernel from module.");
            return -1;
        }

        constexpr u32 thread_group_size = 512;

        auto buffer_a = create_structured_buffer<f32>(
            context_,
            rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess,
            init_data);

        auto buffer_b = create_buffer(
            context_,
            sizeof(f32) * element_count,
            rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess);

        if (!buffer_a || !buffer_b) {
            fmt::println("Failed to create device buffers.");
            return -1;
        }

        auto queue = context_.queue();
        auto encoder = queue->createCommandEncoder();
        if (!encoder) {
            fmt::println("Failed to create command encoder.");
            return -1;
        }

        const auto reduce_times = calc_reduce_times(element_count, thread_group_size);

        rhi::IBuffer *src_buf = buffer_a.get();
        rhi::IBuffer *dst_buf = buffer_b.get();

        for (u32 i = 0, l = element_count; i < reduce_times; i++) {
            auto *pass = encoder->beginComputePass();
            {
                auto root_shader = pass->bindPipeline(kernel.pipeline_.get());
                auto root_cursor = rhi::ShaderCursor(root_shader);

                const u32 group_count = divide_and_round_up(l, thread_group_size);
                const u32 input_byte_size = sizeof(f32) * l;
                const u32 output_byte_size = sizeof(f32) * group_count;
                l = group_count;

                SLANG_RETURN_ON_FAIL(
                    root_cursor["source"].setBinding(rhi::Binding(src_buf, rhi::BufferRange{0, input_byte_size})));
                SLANG_RETURN_ON_FAIL(
                    root_cursor["result"].setBinding(rhi::Binding(dst_buf, rhi::BufferRange{0, output_byte_size})));
                pass->dispatchCompute(group_count, 1, 1);
            }
            pass->end();

            std::swap(src_buf, dst_buf);
        }

        ComPtr<rhi::ICommandBuffer> command_buffer;
        SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
        queue->submit(command_buffer.get());
        queue->waitOnHost();

        // Result is in src_buf (swapped after last pass)
        auto result_view = read_buffer<f32>(context_, src_buf, 0, 1);
        if (!result_view) {
            fmt::println("Failed to read back buffer data from device.");
            return -1;
        }
        const f32 gpu_result = result_view[0];
        fmt::println("reduction(wave-pingpong) result: {}", gpu_result);
        fmt::println("abs error: {}", std::abs(static_cast<f64>(gpu_result) - cpu_result));
        fmt::println("===============================");
    }

    auto reference_buffer = create_structured_buffer<f32>(
        context_,
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
            rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess,
        init_data);
    if (!reference_buffer) {
        fmt::println("Failed to create verification buffer.");
        return -1;
    }

    {
        const auto result_size = pp::reduce_sum_scratch_size<f32>(element_count);
        auto result_buffer = create_buffer(
            context_,
            result_size,
            rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource |
                rhi::BufferUsage::CopyDestination);

        auto queue = context_.queue();
        auto encoder = queue->createCommandEncoder();
        auto gpu_timer = GpuTimer::create(context_, 1);

        {
            const auto timer_scope =
                gpu_timer ? gpu_timer->scope(encoder.get(), "llc::pp::reduce") : GpuTimer::Scope{};
            pp::encode_reduce_sum<f32>(
                context_, encoder.get(), reference_buffer.get(), element_count, result_buffer.get());
        }

        auto command_buffer = encoder->finish();
        queue->submit(command_buffer);
        queue->waitOnHost();

        if (gpu_timer && gpu_timer->resolve()) {
            for (const auto &[label, duration] : gpu_timer->labeled_durations()) {
                fmt::println("[{}] {:.3f} us", label, duration * 1e6);
            }
        }

        auto readback = read_buffer<f32>(context_, result_buffer.get(), 0, 1);
        const f32 llc_reduce_result = readback[0];
        fmt::println("llc::pp::reduce result: {}", llc_reduce_result);
        fmt::println("CPU reference result: {:.0f}", cpu_result);
        fmt::println("llc::pp abs error: {}", std::abs(static_cast<f64>(llc_reduce_result) - cpu_result));
    }

    return 0;
}

} // namespace llc
