#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <fmt/core.h>
#include <vector>
#include <random>
#include "util.hpp"

namespace llc {
using Slang::ComPtr;

inline void diagnose_if_needed(slang::IBlob *diagnostics) {
    if (diagnostics != nullptr) {
        fmt::print("{}", (const char *) diagnostics->getBufferPointer());
    }
}

struct Kernel final {
    ComPtr<rhi::IShaderProgram> program_;
    ComPtr<rhi::IComputePipeline> pipeline_;
    operator bool() const noexcept { return program_ && pipeline_; }
};

struct App final {
    ComPtr<rhi::IDevice> device_;
    ComPtr<slang::ISession> slang_session_;
    ComPtr<slang::IModule> slang_module_;

    Kernel compute_kernel_;
    i32 run(i32 argc, const char *argv[]);
    SlangResult load_kernels();
    ComPtr<slang::IModule> compile_shader_module_from_file(slang::ISession *slang_session, const char *file_path);
    Kernel load_compute_program(slang::IModule *slang_module, const char *entry_point_name);
};

ComPtr<slang::IModule> App::compile_shader_module_from_file(
    slang::ISession *slang_session,
    const char *file_path) {
    ComPtr<slang::IModule> slang_module;
    ComPtr<slang::IBlob> diagnostics;

    slang_module = slang_session->loadModule(file_path, diagnostics.writeRef());
    diagnose_if_needed(diagnostics);

    return slang_module;
}

Kernel App::load_compute_program(slang::IModule *slang_module, const char *entry_point_name) {
    ComPtr<slang::IEntryPoint> entry_point;
    slang_module->findEntryPointByName(entry_point_name, entry_point.writeRef());

    ComPtr<slang::IComponentType> linked_program;
    entry_point->link(linked_program.writeRef());

    Kernel res;

    rhi::ComputePipelineDesc desc;
    auto program = device_->createShaderProgram(linked_program);
    desc.program = program.get();
    res.program_ = program;
    res.pipeline_ = device_->createComputePipeline(desc);
    return res;
}

SlangResult App::load_kernels() {
    const char *kernel_path = "shaders/a+b.slang";

    slang_session_ = device_->getSlangSession();
    slang_module_ = compile_shader_module_from_file(slang_session_.get(), kernel_path);
    if (!slang_module_) {
        fmt::println("Failed to compile shader module from file: {}", kernel_path);
        return SLANG_FAIL;
    }

    compute_kernel_ = load_compute_program(slang_module_.get(), "computeMain");
    if (!compute_kernel_) {
        fmt::println("Failed to load compute program.");
        return SLANG_FAIL;
    }

    return SLANG_OK;
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
    SLANG_RETURN_ON_FAIL(load_kernels());

    /// generate 16k float numbers for each A and B, allocate buffer for input and output
    constexpr usize element_count = 16 * 1024;
    constexpr usize thread_group_size = 128;
    /// Buffer | a0, a1, ..., b0, b1, ..., result0, result1, ...
    std::vector<f32> init_data(element_count * 3);
    {
        // fill A and B with random float numbers
        std::mt19937 rng(42);
        std::uniform_real_distribution<f32> dist(0.0f, 1.0f);
        for (usize i = 0; i < element_count * 2; i++) {
            init_data[i] = dist(rng);
        }
    }

    const u64 segment_byte_size = static_cast<u64>(element_count) * sizeof(f32);
    rhi::BufferDesc buffer_desc{
        .size = init_data.size() * sizeof(f32),
        .elementSize = sizeof(f32),
        .memoryType = rhi::MemoryType::DeviceLocal,
        .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                 rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess,
        .defaultState = rhi::ResourceState::UnorderedAccess,
    };

    auto device_buffer = device_->createBuffer(buffer_desc, init_data.data());
    if (!device_buffer) {
        fmt::println("Failed to create device buffer.");
        return -1;
    }

    /// Note: it seems slang-rhi only supports graphics queue type for now
    auto queue = device_->getQueue(rhi::QueueType::Graphics);
    ComPtr<rhi::ICommandEncoder> encoder;
    queue->createCommandEncoder(encoder.writeRef());
    {
        auto compute_encoder = encoder->beginComputePass();
        auto root_shader_obj = compute_encoder->bindPipeline(compute_kernel_.pipeline_.get());
        rhi::ShaderCursor root_cursor(root_shader_obj);

        const auto bind_segment = [&](const char *name, u64 byte_offset) -> SlangResult {
            rhi::BufferRange range{byte_offset, segment_byte_size};
            return root_cursor[name].setBinding(rhi::Binding(device_buffer, range));
        };

        SLANG_RETURN_ON_FAIL(bind_segment("A", 0));
        SLANG_RETURN_ON_FAIL(bind_segment("B", segment_byte_size));
        SLANG_RETURN_ON_FAIL(bind_segment("result", segment_byte_size * 2));
        u32 group_count = divide_and_round_up(element_count, thread_group_size);
        compute_encoder->dispatchCompute(group_count, 1, 1);
        compute_encoder->end();
    }
    ComPtr<rhi::ICommandBuffer> command_buffer;
    encoder->finish(command_buffer.writeRef());
    queue->submit(command_buffer);

    queue->waitOnHost();
    ComPtr<ISlangBlob> blob;
    const auto byte_size = sizeof(f32) * element_count * 3;
    if (SLANG_FAILED(device_->readBuffer(device_buffer, 0, byte_size, blob.writeRef()))) {
        fmt::println("Failed to read back buffer data from device.");
        return -1;
    }
    auto result_data = reinterpret_cast<const f32 *>(blob->getBufferPointer());

    // verify results
    bool has_mismatch = false;
    for (usize i = 0; i < element_count; i++) {
        f32 expected = init_data[i] + init_data[i + element_count];
        if (result_data[i + element_count * 2] != expected) {
            fmt::println(
                "Result mismatch at index {}: expected {}, got {}",
                i,
                expected,
                result_data[i + element_count * 2]);
            has_mismatch = true;
        }
    }

    if (!has_mismatch) {
        fmt::println("Computation finished successfully with all results matched.");
    }

    return 0;
}

} // namespace llc

int main(int argc, const char *argv[]) {
    llc::App app;
    return app.run(argc, argv);
}