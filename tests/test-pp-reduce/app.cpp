#include "app.h"

#include <vector>

#include <cxxopts.hpp>
#include <fmt/core.h>

#include <llc/buffer.h>
#include <llc/pp/reduce.h>

namespace llc {

namespace {

const auto k_buffer_usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                            rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess;

constexpr u32 k_element_count = 1 << 25;
constexpr u32 k_f16_element_count = 1 << 12; // 4096 — keeps partial sums within f16 range

} // namespace

i32 App::run(i32 argc, const char *argv[]) {
    cxxopts::Options options("test-pp-reduce", "Verify llc::pp::reduce generic instantiations");
    options.add_options()("backend", "RHI backend [dx|vk|auto]", cxxopts::value<std::string>()->default_value("auto"));
    options.parse_positional({"backend"});
    const auto result = options.parse(argc, argv);
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

    // f32
    {
        std::vector<f32> data(k_element_count);
        f64 cpu_sum = 0.0;
        for (usize i = 0; i < k_element_count; ++i) {
            data[i] = static_cast<f32>(i + 1);
            cpu_sum += static_cast<f64>(data[i]);
        }
        auto buffer = create_structured_buffer<f32>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32>(device_.get(), buffer.get(), k_element_count);
        fmt::println("f32:   gpu={} cpu={:.0f} err={}", gpu, cpu_sum, std::abs(static_cast<f64>(gpu) - cpu_sum));
    }

    // f16
    {
        std::vector<f16> data(k_f16_element_count);
        f64 cpu_sum = 0.0;
        for (usize i = 0; i < k_f16_element_count; ++i) {
            data[i] = static_cast<f32>((i % 64) + 1) * 0.01f;
            cpu_sum += static_cast<f64>(static_cast<f32>(data[i]));
        }
        auto buffer = create_structured_buffer<f16>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f16>(device_.get(), buffer.get(), k_f16_element_count);
        fmt::println("f16:   gpu={} cpu={:.0f} err={}", static_cast<f32>(gpu), cpu_sum,
                     std::abs(static_cast<f64>(static_cast<f32>(gpu)) - cpu_sum));
    }

    // f32x4
    {
        std::vector<f32x4> data(k_element_count);
        f64x4 cpu_sum = {0, 0, 0, 0};
        for (usize i = 0; i < k_element_count; ++i) {
            const f32 base = static_cast<f32>(i % 257);
            data[i] = f32x4(base, base * 0.5f, -base, 1.0f);
            cpu_sum += f64x4(data[i]);
        }
        auto buffer = create_structured_buffer<f32x4>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32x4>(device_.get(), buffer.get(), k_element_count);
        auto diff = f64x4(gpu) - cpu_sum;
        fmt::println("f32x4: err=[{}, {}, {}, {}]", std::abs(diff.x), std::abs(diff.y), std::abs(diff.z),
                     std::abs(diff.w));
    }

    // f32x3
    {
        std::vector<f32x3> data(k_element_count);
        f64x3 cpu_sum = {0, 0, 0};
        for (usize i = 0; i < k_element_count; ++i) {
            const f32 base = static_cast<f32>(i % 257);
            data[i] = f32x3(base, base * 0.5f, -base);
            cpu_sum += f64x3(data[i]);
        }
        auto buffer = create_structured_buffer<f32x3>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32x3>(device_.get(), buffer.get(), k_element_count);
        auto diff = f64x3(gpu) - cpu_sum;
        fmt::println("f32x3: err=[{}, {}, {}]", std::abs(diff.x), std::abs(diff.y), std::abs(diff.z));
    }

    // f16x3
    {
        std::vector<f16x3> data;
        data.reserve(k_f16_element_count);
        f64x3 cpu_sum = {0, 0, 0};
        for (usize i = 0; i < k_f16_element_count; ++i) {
            const f32 base = static_cast<f32>((i % 64) + 1) * 0.01f;
            data.emplace_back(base, base * 2.0f, -base * 0.5f);
            cpu_sum += f64x3(f32x3(data.back()));
        }
        auto buffer = create_structured_buffer<f16x3>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f16x3>(device_.get(), buffer.get(), k_f16_element_count);
        auto diff = f64x3(f32x3(gpu)) - cpu_sum;
        fmt::println("f16x3: err=[{}, {}, {}]", std::abs(diff.x), std::abs(diff.y), std::abs(diff.z));
    }

    return 0;
}

} // namespace llc
