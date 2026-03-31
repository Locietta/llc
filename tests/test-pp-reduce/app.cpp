#include "app.h"

#include <vector>

#include <cxxopts.hpp>
#include <fmt/core.h>

#include <llc/buffer.h>
#include <llc/image.h>
#include <llc/pp/reduce.h>
#include <llc/texture.h>

namespace llc {

namespace {

const auto k_buffer_usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                            rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess;

constexpr u32 k_element_count = 1 << 25;
constexpr u32 k_f16_element_count = 1 << 12; // 4096 — keeps partial sums within f16 range
constexpr u32 k_texture_width = 512;
constexpr u32 k_texture_height = 256;
constexpr f64 k_tolerance = 0.001; // 0.1% relative error

/// Returns relative error |gpu - cpu| / |cpu|, or absolute error if cpu is near zero.
f64 relative_error(f64 gpu, f64 cpu) {
    const f64 abs_err = std::abs(gpu - cpu);
    const f64 denom = std::abs(cpu);
    return denom > 1e-12 ? abs_err / denom : abs_err;
}

/// Checks scalar result, prints and returns true on failure.
bool check_scalar(const char *label, f64 gpu, f64 cpu, i32 &failures) {
    const f64 rel = relative_error(gpu, cpu);
    const bool ok = rel <= k_tolerance;
    fmt::println("{}: gpu={} cpu={:.6g} rel_err={:.6e} [{}]", label, gpu, cpu, rel, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
    return ok;
}

/// Checks each component of a 2D vector result.
bool check_vec2(const char *label, f64x2 gpu, f64x2 cpu, i32 &failures) {
    const f64 r0 = relative_error(gpu.x, cpu.x);
    const f64 r1 = relative_error(gpu.y, cpu.y);
    const bool ok = r0 <= k_tolerance && r1 <= k_tolerance;
    fmt::println("{}: rel_err=[{:.6e}, {:.6e}] [{}]", label, r0, r1, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
    return ok;
}

/// Checks each component of a 3D vector result.
bool check_vec3(const char *label, f64x3 gpu, f64x3 cpu, i32 &failures) {
    const f64 r0 = relative_error(gpu.x, cpu.x);
    const f64 r1 = relative_error(gpu.y, cpu.y);
    const f64 r2 = relative_error(gpu.z, cpu.z);
    const bool ok = r0 <= k_tolerance && r1 <= k_tolerance && r2 <= k_tolerance;
    fmt::println("{}: rel_err=[{:.6e}, {:.6e}, {:.6e}] [{}]", label, r0, r1, r2, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
    return ok;
}

/// Checks each component of a 4D vector result.
bool check_vec4(const char *label, f64x4 gpu, f64x4 cpu, i32 &failures) {
    const f64 r0 = relative_error(gpu.x, cpu.x);
    const f64 r1 = relative_error(gpu.y, cpu.y);
    const f64 r2 = relative_error(gpu.z, cpu.z);
    const f64 r3 = relative_error(gpu.w, cpu.w);
    const bool ok = r0 <= k_tolerance && r1 <= k_tolerance && r2 <= k_tolerance && r3 <= k_tolerance;
    fmt::println("{}: rel_err=[{:.6e}, {:.6e}, {:.6e}, {:.6e}] [{}]", label, r0, r1, r2, r3, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
    return ok;
}

} // namespace

i32 App::run(i32 argc, const char *argv[]) {
    cxxopts::Options options("test-pp-reduce", "Verify llc::pp::reduce generic instantiations");
    options.add_options()("backend", "RHI backend [dx|vk|cpu|auto]", cxxopts::value<std::string>()->default_value("auto"));
    options.parse_positional({"backend"});
    const auto result = options.parse(argc, argv);
    const std::string backend_name = result["backend"].as<std::string>();

    rhi::DeviceDesc device_desc;
    device_desc.slang.targetProfile = "spirv_1_6";
    device_desc.deviceType = rhi::DeviceType::Vulkan;

    device_ = rhi::getRHI()->createDevice(device_desc);
    if (!device_) {
        fmt::println("Failed to create RHI device.");
        return -1;
    }

    i32 failures = 0;

    // f32
    {
        std::vector<f32> data(k_element_count);
        f64 cpu_sum = 0.0;
        for (usize i = 0; i < k_element_count; ++i) {
            data[i] = static_cast<f32>(i + 1);
            cpu_sum += static_cast<f64>(data[i]);
        }
        auto buffer = create_buffer<f32>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32>(device_.get(), buffer.get(), k_element_count);
        check_scalar("f32", static_cast<f64>(gpu), cpu_sum, failures);
    }

    // f16
    {
        std::vector<f16> data(k_f16_element_count);
        f64 cpu_sum = 0.0;
        for (usize i = 0; i < k_f16_element_count; ++i) {
            data[i] = static_cast<f32>((i % 64) + 1) * 0.01f;
            cpu_sum += static_cast<f64>(static_cast<f32>(data[i]));
        }
        auto buffer = create_buffer<f16>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f16>(device_.get(), buffer.get(), k_f16_element_count);
        check_scalar("f16", static_cast<f64>(static_cast<f32>(gpu)), cpu_sum, failures);
    }

    // f32x2
    {
        std::vector<f32x2> data(k_element_count);
        f64x2 cpu_sum = {0, 0};
        for (usize i = 0; i < k_element_count; ++i) {
            const f32 base = static_cast<f32>(i % 257);
            data[i] = f32x2(base, -base * 0.5f);
            cpu_sum += f64x2(data[i]);
        }
        auto buffer = create_buffer<f32x2>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32x2>(device_.get(), buffer.get(), k_element_count);
        check_vec2("f32x2", f64x2(gpu), cpu_sum, failures);
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
        auto buffer = create_buffer<f32x3>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32x3>(device_.get(), buffer.get(), k_element_count);
        check_vec3("f32x3", f64x3(gpu), cpu_sum, failures);
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
        auto buffer = create_buffer<f32x4>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f32x4>(device_.get(), buffer.get(), k_element_count);
        check_vec4("f32x4", f64x4(gpu), cpu_sum, failures);
    }

    // f16x2
    {
        std::vector<f16x2> data(k_f16_element_count);
        f64x2 cpu_sum = {0, 0};
        for (usize i = 0; i < k_f16_element_count; ++i) {
            const f32 base = static_cast<f32>((i % 64) + 1) * 0.01f;
            data[i] = f16x2(base, -base * 0.5f);
            cpu_sum += f64x2(f32x2(data[i]));
        }
        auto buffer = create_buffer<f16x2>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f16x2>(device_.get(), buffer.get(), k_f16_element_count);
        check_vec2("f16x2", f64x2(f32x2(gpu)), cpu_sum, failures);
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
        auto buffer = create_buffer<f16x3>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f16x3>(device_.get(), buffer.get(), k_f16_element_count);
        check_vec3("f16x3", f64x3(f32x3(gpu)), cpu_sum, failures);
    }

    // f16x4
    {
        std::vector<f16x4> data(k_f16_element_count);
        f64x4 cpu_sum = {0, 0, 0, 0};
        for (usize i = 0; i < k_f16_element_count; ++i) {
            const f32 base = static_cast<f32>((i % 64) + 1) * 0.01f;
            data[i] = f16x4(base, base * 2.0f, -base * 0.5f, base * 0.25f);
            cpu_sum += f64x4(f32x4(data[i]));
        }
        auto buffer = create_buffer<f16x4>(device_.get(), k_buffer_usage, data);
        auto gpu = pp::reduce_sum<f16x4>(device_.get(), buffer.get(), k_f16_element_count);
        check_vec4("f16x4", f64x4(f32x4(gpu)), cpu_sum, failures);
    }

    // texture f32
    {
        Image image(k_texture_width, k_texture_height, rhi::Format::R32Float, k_texture_width * sizeof(f32));
        auto view = image.view<f32>();
        f64 cpu_sum = 0.0;
        for (u32 y = 0; y < k_texture_height; ++y) {
            for (u32 x = 0; x < k_texture_width; ++x) {
                const auto index = static_cast<usize>(y) * k_texture_width + x;
                const auto value = static_cast<f32>(index % 257);
                view[y, x] = value;
                cpu_sum += static_cast<f64>(value);
            }
        }

        auto texture = create_texture_2d(device_.get(), image);
        auto gpu = pp::reduce_texture_sum<f32>(device_.get(), texture.get());
        check_scalar("texture f32", static_cast<f64>(gpu), cpu_sum, failures);
    }

    // texture f32x4
    {
        Image image(k_texture_width, k_texture_height, rhi::Format::RGBA32Float, k_texture_width * sizeof(f32x4));
        auto view = image.view<f32x4>();
        f64x4 cpu_sum = {0, 0, 0, 0};
        for (u32 y = 0; y < k_texture_height; ++y) {
            for (u32 x = 0; x < k_texture_width; ++x) {
                const auto index = static_cast<usize>(y) * k_texture_width + x;
                const auto base = static_cast<f32>(index % 257);
                const auto value = f32x4(base, base * 0.5f, -base, 1.0f);
                view[y, x] = value;
                cpu_sum += f64x4(value);
            }
        }

        auto texture = create_texture_2d(device_.get(), image);
        auto gpu = pp::reduce_texture_sum<f32x4>(device_.get(), texture.get());
        check_vec4("texture f32x4", f64x4(gpu), cpu_sum, failures);
    }

    constexpr i32 k_test_count = 10;
    fmt::println("\n{}/{} tests passed", k_test_count - failures, k_test_count);
    return failures > 0 ? 1 : 0;
}

} // namespace llc
