#include "reduce.h"

#include <mutex>
#include <vector>

#include <slang-rhi/shader-cursor.h>

#include <llc/blob.h>
#include <llc/buffer.h>
#include <llc/math.h>

namespace llc::pp {

namespace {

using namespace llc::types;

extern "C" const unsigned char _binary_reduce_slang_module_start[]; // NOLINT
extern "C" const unsigned char _binary_reduce_slang_module_end[];   // NOLINT

constexpr u32 k_thread_group_size = 256;

enum class ReducePipelineKind : u32 {
    BUFFER,
    TEXTURE,
};

struct EmbeddedModuleDesc final {
    const char *module_name = nullptr;
    const unsigned char *begin = nullptr;
    const unsigned char *end = nullptr;
};

struct CachedPipeline final {
    rhi::IDevice *device = nullptr;
    ReducePipelineKind kind = ReducePipelineKind::BUFFER;
    Slang::ComPtr<rhi::IComputePipeline> pipeline;
};

std::mutex g_pipeline_mutex;
std::vector<CachedPipeline> g_pipelines;

u32 next_reduce_count(u32 count) noexcept {
    if (count == 0) return 0;
    return divide_and_round_up(count, k_thread_group_size * 2);
}

u64 scratch_size_for_element_count(u32 count) noexcept {
    u64 scratch_size = 0;
    for (u32 level_count = next_reduce_count(count); level_count > 1; level_count = next_reduce_count(level_count)) {
        scratch_size += sizeof(f32) * static_cast<u64>(level_count);
    }
    return scratch_size;
}

EmbeddedModuleDesc reduce_module_desc() noexcept {
    return {
        .module_name = "reduce",
        .begin = _binary_reduce_slang_module_start,
        .end = _binary_reduce_slang_module_end,
    };
}

Slang::ComPtr<slang::IModule> load_embedded_shader_module(rhi::IDevice *device) {
    const auto desc = reduce_module_desc();
    if (!desc.module_name || !desc.begin || !desc.end || desc.begin >= desc.end) {
        return nullptr;
    }

    const auto shader_ir = Slang::ComPtr<FileBlob>(new FileBlob(std::span<const byte>(
        reinterpret_cast<const byte *>(desc.begin),
        static_cast<usize>(desc.end - desc.begin))));

    Slang::ComPtr<slang::IBlob> diagnostics;
    auto *module_ptr = device->getSlangSession()->loadModuleFromIRBlob(
        desc.module_name,
        desc.module_name,
        shader_ir.get(),
        diagnostics.writeRef());
    diagnose_if_needed(diagnostics.get());
    return Slang::ComPtr<slang::IModule>(module_ptr);
}

const char *entry_point_name(ReducePipelineKind kind) noexcept {
    switch (kind) {
        case ReducePipelineKind::BUFFER: return "reduceBuffer";
        case ReducePipelineKind::TEXTURE: return "reduceTexture";
        default: return nullptr;
    }
}

Slang::ComPtr<rhi::IComputePipeline> create_reduce_pipeline(rhi::IDevice *device, ReducePipelineKind kind) {
    auto module = load_embedded_shader_module(device);
    if (!module) return nullptr;

    Slang::ComPtr<slang::IEntryPoint> entry_point;
    if (SLANG_FAILED(module->findEntryPointByName(entry_point_name(kind), entry_point.writeRef()))) {
        return nullptr;
    }

    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IComponentType> linked_program;
    if (SLANG_FAILED(entry_point->link(linked_program.writeRef(), diagnostics.writeRef()))) {
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

Slang::ComPtr<rhi::IComputePipeline> get_reduce_pipeline(rhi::IDevice *device, ReducePipelineKind kind) {
    {
        std::scoped_lock lock(g_pipeline_mutex);
        for (const auto &cached : g_pipelines) {
            if (cached.device == device && cached.kind == kind) {
                return cached.pipeline;
            }
        }
    }

    auto pipeline = create_reduce_pipeline(device, kind);
    if (!pipeline) return nullptr;

    std::scoped_lock lock(g_pipeline_mutex);
    for (const auto &cached : g_pipelines) {
        if (cached.device == device && cached.kind == kind) {
            return cached.pipeline;
        }
    }
    g_pipelines.push_back({device, kind, pipeline});
    return pipeline;
}

SlangResult encode_buffer_pass(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::IBuffer *source,
    u64 source_offset,
    u32 count,
    rhi::IBuffer *result,
    u64 result_offset) {

    auto pipeline = get_reduce_pipeline(device, ReducePipelineKind::BUFFER);
    if (!pipeline) return SLANG_FAIL;

    const u32 group_count = next_reduce_count(count);
    auto *pass = encoder->beginComputePass();
    auto root_object = pass->bindPipeline(pipeline.get());
    auto cursor = rhi::ShaderCursor(root_object);
    SLANG_RETURN_ON_FAIL(cursor["source"].setBinding(rhi::Binding(
        source,
        rhi::BufferRange{source_offset, sizeof(f32) * static_cast<u64>(count)})));
    SLANG_RETURN_ON_FAIL(cursor["result"].setBinding(rhi::Binding(
        result,
        rhi::BufferRange{result_offset, sizeof(f32) * static_cast<u64>(group_count)})));
    SLANG_RETURN_ON_FAIL(cursor["sourceCount"].setData(&count, sizeof(count)));
    pass->dispatchCompute(group_count, 1, 1);
    pass->end();
    return SLANG_OK;
}

SlangResult encode_texture_pass(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::ITexture *source,
    u32 width,
    u32 height,
    rhi::IBuffer *result,
    u64 result_offset) {

    auto pipeline = get_reduce_pipeline(device, ReducePipelineKind::TEXTURE);
    if (!pipeline) return SLANG_FAIL;

    const u32 element_count = width * height;
    const u32 group_count = next_reduce_count(element_count);
    auto *pass = encoder->beginComputePass();
    auto root_object = pass->bindPipeline(pipeline.get());
    auto cursor = rhi::ShaderCursor(root_object);
    const u32 texture_size[2] = {width, height};
    SLANG_RETURN_ON_FAIL(cursor["sourceTexture"].setBinding(source));
    SLANG_RETURN_ON_FAIL(cursor["result"].setBinding(rhi::Binding(
        result,
        rhi::BufferRange{result_offset, sizeof(f32) * static_cast<u64>(group_count)})));
    SLANG_RETURN_ON_FAIL(cursor["textureSize"].setData(texture_size, sizeof(texture_size)));
    SLANG_RETURN_ON_FAIL(cursor["sourceCount"].setData(&element_count, sizeof(element_count)));
    pass->dispatchCompute(group_count, 1, 1);
    pass->end();
    return SLANG_OK;
}

SlangResult encode_buffer_reduce_internal(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::IBuffer *source,
    u64 source_offset,
    u32 count,
    rhi::IBuffer *scratch,
    u64 scratch_offset,
    rhi::IBuffer *result) {

    if (count == 0) {
        encoder->clearBuffer(result, rhi::BufferRange{0, sizeof(f32)});
        return SLANG_OK;
    }
    if (count == 1) {
        encoder->copyBuffer(result, 0, source, source_offset, sizeof(f32));
        return SLANG_OK;
    }

    u32 level_count = count;
    rhi::IBuffer *level_source = source;
    u64 level_source_offset = source_offset;
    u64 next_scratch_offset = scratch_offset;

    while (true) {
        const u32 output_count = next_reduce_count(level_count);
        const bool final_level = output_count == 1;
        auto *level_result = final_level ? result : scratch;
        const u64 level_result_offset = final_level ? 0 : next_scratch_offset;
        SLANG_RETURN_ON_FAIL(encode_buffer_pass(
            device,
            encoder,
            level_source,
            level_source_offset,
            level_count,
            level_result,
            level_result_offset));
        if (final_level) {
            return SLANG_OK;
        }
        level_source = scratch;
        level_source_offset = next_scratch_offset;
        level_count = output_count;
        next_scratch_offset += sizeof(f32) * static_cast<u64>(output_count);
    }
}

} // namespace

u64 reduce_sum_scratch_size_f32(u32 count) {
    return scratch_size_for_element_count(count);
}

SlangResult encode_reduce_sum_f32(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::IBuffer *source,
    u32 count,
    rhi::IBuffer *scratch,
    u64 scratch_offset,
    rhi::IBuffer *result) {

    if (!device || !encoder || !source || !result) {
        return SLANG_E_INVALID_ARG;
    }
    if (reduce_sum_scratch_size_f32(count) > 0 && !scratch) {
        return SLANG_E_INVALID_ARG;
    }

    return encode_buffer_reduce_internal(device, encoder, source, 0, count, scratch, scratch_offset, result);
}

f32 reduce_sum_f32(rhi::IDevice *device, rhi::IBuffer *source, u32 count) {
    if (!device || !source) return 0.0f;

    const auto scratch_size = reduce_sum_scratch_size_f32(count);
    auto scratch = scratch_size == 0 ?
                       Slang::ComPtr<rhi::IBuffer>() :
                       create_structured_buffer(
                           device,
                           scratch_size,
                           sizeof(f32),
                           rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess);
    auto result = create_structured_buffer(
        device,
        sizeof(f32),
        sizeof(f32),
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource |
            rhi::BufferUsage::CopyDestination,
        nullptr);

    if (!result || (scratch_size > 0 && !scratch)) return 0.0f;

    auto queue = device->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    if (SLANG_FAILED(encode_reduce_sum_f32(device, encoder.get(), source, count, scratch.get(), 0, result.get()))) {
        return 0.0f;
    }

    auto command_buffer = encoder->finish();
    queue->submit(command_buffer);
    queue->waitOnHost();

    auto readback = read_buffer<f32>(device, result.get(), 0, 1);
    return readback ? readback[0] : 0.0f;
}

u64 reduce_sum_texture_r32f_scratch_size(u32 width, u32 height) {
    if (width == 0 || height == 0) return 0;
    return reduce_sum_scratch_size_f32(width * height);
}

SlangResult encode_reduce_sum_texture_r32f(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::ITexture *source,
    u32 width,
    u32 height,
    rhi::IBuffer *scratch,
    u64 scratch_offset,
    rhi::IBuffer *result) {

    if (!device || !encoder || !source || !result || width == 0 || height == 0) {
        return SLANG_E_INVALID_ARG;
    }

    const u32 element_count = width * height;
    const auto scratch_size = reduce_sum_texture_r32f_scratch_size(width, height);
    if (scratch_size > 0 && !scratch) {
        return SLANG_E_INVALID_ARG;
    }
    if (element_count == 1) {
        return encode_texture_pass(device, encoder, source, width, height, result, 0);
    }

    const u32 partial_count = next_reduce_count(element_count);
    if (partial_count == 1) {
        return encode_texture_pass(device, encoder, source, width, height, result, 0);
    }

    SLANG_RETURN_ON_FAIL(encode_texture_pass(device, encoder, source, width, height, scratch, scratch_offset));
    return encode_buffer_reduce_internal(
        device,
        encoder,
        scratch,
        scratch_offset,
        partial_count,
        scratch,
        scratch_offset + sizeof(f32) * static_cast<u64>(partial_count),
        result);
}

f32 reduce_sum_texture_r32f(rhi::IDevice *device, rhi::ITexture *source, u32 width, u32 height) {
    if (!device || !source || width == 0 || height == 0) {
        return 0.0f;
    }

    const auto scratch_size = reduce_sum_texture_r32f_scratch_size(width, height);
    auto scratch = scratch_size == 0 ?
                       Slang::ComPtr<rhi::IBuffer>() :
                       create_structured_buffer(
                           device,
                           scratch_size,
                           sizeof(f32),
                           rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess);
    auto result = create_structured_buffer(
        device,
        sizeof(f32),
        sizeof(f32),
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource |
            rhi::BufferUsage::CopyDestination,
        nullptr);

    if (!result || (scratch_size > 0 && !scratch)) return 0.0f;

    auto queue = device->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    if (SLANG_FAILED(encode_reduce_sum_texture_r32f(
            device,
            encoder.get(),
            source,
            width,
            height,
            scratch.get(),
            0,
            result.get()))) {
        return 0.0f;
    }

    auto command_buffer = encoder->finish();
    queue->submit(command_buffer);
    queue->waitOnHost();

    auto readback = read_buffer<f32>(device, result.get(), 0, 1);
    return readback ? readback[0] : 0.0f;
}

} // namespace llc::pp
