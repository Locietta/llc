#include "reduce.h"

#include <cassert>
#include <mutex>
#include <string>
#include <vector>

#include <slang-rhi/shader-cursor.h>

#include <llc/blob.h>
#include <llc/buffer.h>
#include <llc/math.h>
#include <llc/span.h>

namespace llc::pp {

namespace {

using namespace llc::types;

extern "C" const unsigned char _binary_span_slang_module_start[];   // NOLINT
extern "C" const unsigned char _binary_span_slang_module_end[];     // NOLINT
extern "C" const unsigned char _binary_reduce_slang_module_start[]; // NOLINT
extern "C" const unsigned char _binary_reduce_slang_module_end[];   // NOLINT

constexpr usize k_thread_group_size = 256;

template <typename T>
struct ReduceTypeInfo;

#define LLC_REDUCE_CONFIG(slang_type)                                            \
    "import reduce;\n"                                                           \
    "struct Impl : IReduceElement {\n"                                           \
    "    " slang_type " value;\n"                                                \
    "    __init(int v) { value = " slang_type "(v); }\n"                         \
    "    __init(" slang_type " v) { value = v; }\n"                              \
    "    This dadd(This other) { return This(value + other.value); }\n"          \
    "    static This waveSum(This a) { return This(WaveActiveSum(a.value)); }\n" \
    "};\n"                                                                       \
    "export struct ReduceElement : IReduceElement = Impl;\n"

#define LLC_DEFINE_REDUCE_TYPE_INFO(shader_type, cpp_type)                         \
    template <>                                                                    \
    struct ReduceTypeInfo<cpp_type> final {                                        \
        static constexpr const char *k_slang_type = shader_type;                   \
        static constexpr usize k_byte_size = sizeof(cpp_type);                     \
        static constexpr const char *k_config_name = "reduce_config_" shader_type; \
        static constexpr const char *k_config_source =                             \
            LLC_REDUCE_CONFIG(shader_type);                                        \
    }

LLC_DEFINE_REDUCE_TYPE_INFO("float", f32);
LLC_DEFINE_REDUCE_TYPE_INFO("half", f16);
LLC_DEFINE_REDUCE_TYPE_INFO("float2", f32x2);
LLC_DEFINE_REDUCE_TYPE_INFO("float3", f32x3);
LLC_DEFINE_REDUCE_TYPE_INFO("float4", f32x4);
LLC_DEFINE_REDUCE_TYPE_INFO("vector<half, 2>", f16x2);
LLC_DEFINE_REDUCE_TYPE_INFO("vector<half, 3>", f16x3);
LLC_DEFINE_REDUCE_TYPE_INFO("vector<half, 4>", f16x4);

Slang::ComPtr<slang::IModule> load_embedded_ir(
    rhi::IDevice *device,
    const char *name,
    const unsigned char *start,
    const unsigned char *end) {

    auto blob = Slang::ComPtr<FileBlob>(new FileBlob(std::span<const byte>(
        reinterpret_cast<const byte *>(start),
        reinterpret_cast<const byte *>(end))));

    Slang::ComPtr<slang::IBlob> diagnostics;
    auto *module_ptr = device->getSlangSession()->loadModuleFromIRBlob(
        name, name, blob.get(), diagnostics.writeRef());
    diagnose_if_needed(diagnostics.get());
    return Slang::ComPtr<slang::IModule>(module_ptr);
}

void load_span_module(rhi::IDevice *device) {
    load_embedded_ir(device, "span",
                     _binary_span_slang_module_start,
                     _binary_span_slang_module_end);
}

Slang::ComPtr<slang::IModule> load_reduce_module(rhi::IDevice *device) {
    load_span_module(device);
    return load_embedded_ir(device, "reduce",
                            _binary_reduce_slang_module_start,
                            _binary_reduce_slang_module_end);
}

Slang::ComPtr<rhi::IComputePipeline> create_linked_pipeline(
    rhi::IDevice *device,
    slang::IModule *main_module,
    const std::string &config_name,
    const std::string &config_source,
    const char *entry_point_name) {

    if (!device || !main_module) return nullptr;

    auto session = device->getSlangSession();

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule *config_module = session->loadModuleFromSourceString(
        config_name.c_str(),
        config_name.c_str(),
        config_source.c_str(),
        diagnostics.writeRef());
    diagnose_if_needed(diagnostics.get());
    if (!config_module) return nullptr;

    Slang::ComPtr<slang::IEntryPoint> entry_point;
    if (SLANG_FAILED(main_module->findEntryPointByName(entry_point_name, entry_point.writeRef()))) {
        return nullptr;
    }

    slang::IComponentType *components[] = {main_module, config_module, entry_point.get()};
    Slang::ComPtr<slang::IComponentType> composed;
    diagnostics = nullptr;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components,
            std::size(components),
            composed.writeRef(),
            diagnostics.writeRef()))) {
        diagnose_if_needed(diagnostics.get());
        return nullptr;
    }

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics = nullptr;
    if (SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef()))) {
        diagnose_if_needed(diagnostics.get());
        return nullptr;
    }

    auto program = device->createShaderProgram(linked);
    if (!program) return nullptr;

    rhi::ComputePipelineDesc desc{};
    desc.program = program.get();
    return device->createComputePipeline(desc);
}

struct CachedPipeline final {
    rhi::IDevice *device = nullptr;
    std::string key;
    Slang::ComPtr<rhi::IComputePipeline> pipeline;
};

struct PipelineCache final {
    std::mutex mutex;
    std::vector<CachedPipeline> entries;
};

PipelineCache g_buffer_pipelines;

template <typename CreateFn>
Slang::ComPtr<rhi::IComputePipeline>
get_cached_pipeline(PipelineCache &cache, rhi::IDevice *device, std::string key, CreateFn create_fn) {
    {
        std::scoped_lock lock(cache.mutex);
        for (const auto &cached : cache.entries) {
            if (cached.device == device && cached.key == key) {
                return cached.pipeline;
            }
        }
    }

    auto pipeline = create_fn(device);
    if (!pipeline) return nullptr;

    std::scoped_lock lock(cache.mutex);
    for (const auto &cached : cache.entries) {
        if (cached.device == device && cached.key == key) {
            return cached.pipeline;
        }
    }
    cache.entries.push_back({device, std::move(key), pipeline});
    return pipeline;
}

constexpr usize next_reduce_count(usize count) noexcept {
    return divide_and_round_up(count, k_thread_group_size * 2);
}

SlangResult encode_buffer_pass(
    rhi::ICommandEncoder *encoder,
    rhi::IComputePipeline *pipeline,
    rhi::IBuffer *source,
    u64 count,
    rhi::IBuffer *result,
    u32 element_byte_size) {

    const u32 group_count = next_reduce_count(count);
    auto *pass = encoder->beginComputePass();
    auto root_object = pass->bindPipeline(pipeline);
    auto cursor = rhi::ShaderCursor(root_object);
    const GpuSpan source_span{source->getDeviceAddress(), count};
    const GpuSpan result_span{result->getDeviceAddress(), static_cast<u64>(group_count)};
    SLANG_RETURN_ON_FAIL(cursor["source"].setData(source_span));
    SLANG_RETURN_ON_FAIL(cursor["result"].setData(result_span));
    pass->dispatchCompute(group_count, 1, 1);
    pass->end();
    return SLANG_OK;
}

} // namespace

template <typename T>
usize reduce_sum_scratch_size(usize count) {
    return next_reduce_count(count) * ReduceTypeInfo<T>::k_byte_size;
}

template <typename T>
SlangResult encode_reduce_sum(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::IBuffer *source,
    usize count,
    rhi::IBuffer *result) {

    assert(device && encoder && source && result);

    using Info = ReduceTypeInfo<T>;
    auto pipeline = get_cached_pipeline(g_buffer_pipelines, device, Info::k_slang_type, [](rhi::IDevice *d) {
        auto module = load_reduce_module(d);
        if (!module) return Slang::ComPtr<rhi::IComputePipeline>{};
        return create_linked_pipeline(d, module.get(), Info::k_config_name, Info::k_config_source, "reduce");
    });
    if (!pipeline) return SLANG_FAIL;

    constexpr u32 elem_size = Info::k_byte_size;
    const auto initial_count = static_cast<u64>(count);

    bool first = true;
    for (u64 l = initial_count; l > 1; l = next_reduce_count(l)) {
        if (!first) encoder->globalBarrier();
        first = false;
        auto *src = (l == initial_count) ? source : result;
        SLANG_RETURN_ON_FAIL(encode_buffer_pass(encoder, pipeline.get(), src, l, result, elem_size));
    }
    return SLANG_OK;
}

template <typename T>
T reduce_sum(rhi::IDevice *device, rhi::IBuffer *source, usize count) {
    assert(device && source);

    const auto result_size = reduce_sum_scratch_size<T>(count);
    auto result = create_buffer(
        device,
        result_size,
        rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource | rhi::BufferUsage::CopyDestination);

    auto queue = device->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encode_reduce_sum<T>(device, encoder.get(), source, count, result.get());

    auto command_buffer = encoder->finish();
    queue->submit(command_buffer);
    queue->waitOnHost();

    auto readback = read_buffer<T>(device, result.get(), 0, 1);
    return readback[0];
}

// clang-format off
#define LLC_INSTANTIATE_REDUCE(T)                                                                                     \
    template usize reduce_sum_scratch_size<T>(usize);                                                                 \
    template SlangResult encode_reduce_sum<T>(                                                                        \
        rhi::IDevice *, rhi::ICommandEncoder *, rhi::IBuffer *, usize, rhi::IBuffer *);                               \
    template T reduce_sum<T>(rhi::IDevice *, rhi::IBuffer *, usize);

LLC_INSTANTIATE_REDUCE(f32)
LLC_INSTANTIATE_REDUCE(f16)
LLC_INSTANTIATE_REDUCE(f32x2)
LLC_INSTANTIATE_REDUCE(f32x3)
LLC_INSTANTIATE_REDUCE(f32x4)
LLC_INSTANTIATE_REDUCE(f16x2)
LLC_INSTANTIATE_REDUCE(f16x3)
LLC_INSTANTIATE_REDUCE(f16x4)
// clang-format on

#undef LLC_INSTANTIATE_REDUCE

} // namespace llc::pp
