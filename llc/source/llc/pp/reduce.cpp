#include "reduce.h"

#include <cassert>
#include <string>
#include <vector>

#include <slang-rhi/shader-cursor.h>

#include <llc/blob.h>
#include <llc/buffer.h>
#include <llc/math.h>
#include <llc/span.h>
#include <llc/texture.h>

#include <llc/utils/embedded_module.h>
#include <llc/utils/pipeline_cache.h>

namespace llc::pp {

namespace {

using namespace llc::types;

extern "C" const unsigned char _binary_reduce_slang_module_start[]; // NOLINT(readability-identifier-naming)
extern "C" const unsigned char _binary_reduce_slang_module_end[];   // NOLINT(readability-identifier-naming)

constexpr usize k_thread_group_size = 256;

/// Create a child Slang session matching the device's backend, with warning E41012
/// ("profile implicitly upgraded") suppressed. This is expected when linking
/// target-agnostic precompiled IR modules into a profiled session.
Slang::ComPtr<slang::ISession> create_linking_session(rhi::IDevice *device) {
    auto session = device->getSlangSession();
    auto *global = session->getGlobalSession();

    SlangCompileTarget target = SLANG_SPIRV;
    const char *profile = "spirv_1_6";
    if (device->getDeviceType() == rhi::DeviceType::D3D12) {
        target = SLANG_DXIL;
        profile = "sm_6_6";
    }

    slang::CompilerOptionEntry disable_41012{};
    disable_41012.name = slang::CompilerOptionName::DisableWarning;
    disable_41012.value.kind = slang::CompilerOptionValueKind::String;
    disable_41012.value.stringValue0 = "41012";

    slang::TargetDesc target_desc{};
    target_desc.format = target;
    target_desc.profile = global->findProfile(profile);
    target_desc.forceGLSLScalarBufferLayout = true;

    slang::SessionDesc desc{};
    desc.targets = &target_desc;
    desc.targetCount = 1;
    desc.compilerOptionEntries = &disable_41012;
    desc.compilerOptionEntryCount = 1;
    desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

    Slang::ComPtr<slang::ISession> child;
    if (SLANG_FAILED(global->createSession(desc, child.writeRef()))) {
        return nullptr;
    }
    return child;
}

template <typename T>
struct ReduceTypeInfo;

template <typename T>
struct ReduceTextureTypeInfo;

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

#define LLC_REDUCE_TEXTURE_CONFIG(reduce_module_name, texel_type)                  \
    "import " reduce_module_name ";\n"                                             \
    "export struct ReduceTexture {\n"                                              \
    "    Texture2D<" texel_type "> texture;\n"                                     \
    "    ReduceElement load(uint2 sourceSize, uint index) {\n"                     \
    "        if (index >= sourceSize.x * sourceSize.y) return ReduceElement(0);\n" \
    "        uint x = index % sourceSize.x;\n"                                     \
    "        uint y = index / sourceSize.x;\n"                                     \
    "        return ReduceElement(texture.Load(int3(int(x), int(y), 0)));\n"      \
    "    }\n"                                                                      \
    "};\n"

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
LLC_DEFINE_REDUCE_TYPE_INFO("vector<half, 2>", f16x2);
LLC_DEFINE_REDUCE_TYPE_INFO("vector<half, 3>", f16x3);
LLC_DEFINE_REDUCE_TYPE_INFO("vector<half, 4>", f16x4);

template <>
struct ReduceTypeInfo<f32x4> final {
    static constexpr const char *k_slang_type = "float4";
    static constexpr usize k_byte_size = sizeof(f32x4);
    static constexpr const char *k_config_name = "reduce_config_float4";
    static constexpr const char *k_config_source =
        "import reduce;\n"
        "export struct ReduceElement : IReduceElement {\n"
        "    float4 value;\n"
        "    __init(int v) { value = float4(v); }\n"
        "    __init(float4 v) { value = v; }\n"
        "    This dadd(This other) { return This(value + other.value); }\n"
        "    static This waveSum(This a) { return This(WaveActiveSum(a.value)); }\n"
        "};\n";
};

#define LLC_DEFINE_REDUCE_TEXTURE_TYPE_INFO(cpp_type, format, shader_type)              \
    template <>                                                                          \
    struct ReduceTextureTypeInfo<cpp_type> final {                                       \
        static constexpr auto k_format = format;                                         \
        static constexpr const char *k_config_name = "reduce_texture_config_" shader_type; \
        static constexpr const char *k_config_source =                                   \
            LLC_REDUCE_TEXTURE_CONFIG("reduce_config_" shader_type, shader_type);        \
        static constexpr const char *k_pipeline_key = "reduce_texture_" shader_type;     \
    }

LLC_DEFINE_REDUCE_TEXTURE_TYPE_INFO(f32, rhi::Format::R32Float, "float");
LLC_DEFINE_REDUCE_TEXTURE_TYPE_INFO(f32x4, rhi::Format::RGBA32Float, "float4");

Slang::ComPtr<rhi::IComputePipeline> create_linked_pipeline(
    rhi::IDevice *device,
    slang::ISession *session,
    slang::IModule *main_module,
    const std::string &config_name,
    const std::string &config_source,
    const char *entry_point_name,
    const slang::SpecializationArg *specialization_args = nullptr,
    usize specialization_arg_count = 0) {

    if (!device || !session || !main_module) return nullptr;

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

    Slang::ComPtr<slang::IComponentType> specialized_entry_point;
    slang::IComponentType *entry_point_component = entry_point.get();
    if (specialization_arg_count > 0) {
        diagnostics = nullptr;
        if (SLANG_FAILED(entry_point->specialize(
                specialization_args,
                static_cast<SlangInt>(specialization_arg_count),
                specialized_entry_point.writeRef(),
                diagnostics.writeRef()))) {
            diagnose_if_needed(diagnostics.get());
            return nullptr;
        }
        diagnose_if_needed(diagnostics.get());
        entry_point_component = specialized_entry_point.get();
    }

    slang::IComponentType *components[] = {main_module, config_module, entry_point_component};
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

template <typename T>
Slang::ComPtr<rhi::IComputePipeline> create_linked_texture_pipeline(rhi::IDevice *device) {
    using ReduceInfo = ReduceTypeInfo<T>;
    using TextureInfo = ReduceTextureTypeInfo<T>;

    auto session = create_linking_session(device);
    if (!session) return nullptr;

    auto span = load_span_module(session.get());
    auto reduce = load_embedded_module(session.get(), EmbededModuleDesc{
                                                          .name = "reduce",
                                                          .start = _binary_reduce_slang_module_start,
                                                          .end = _binary_reduce_slang_module_end,
                                                      });
    if (!span || !reduce) return nullptr;

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule *reduce_element_module = session->loadModuleFromSourceString(
        ReduceInfo::k_config_name,
        ReduceInfo::k_config_name,
        ReduceInfo::k_config_source,
        diagnostics.writeRef());
    diagnose_if_needed(diagnostics.get());
    if (!reduce_element_module) return nullptr;

    diagnostics = nullptr;
    slang::IModule *texture_module = session->loadModuleFromSourceString(
        TextureInfo::k_config_name,
        TextureInfo::k_config_name,
        TextureInfo::k_config_source,
        diagnostics.writeRef());
    diagnose_if_needed(diagnostics.get());
    if (!texture_module) return nullptr;

    Slang::ComPtr<slang::IEntryPoint> entry_point;
    if (SLANG_FAILED(reduce->findEntryPointByName("reduce_texture", entry_point.writeRef()))) {
        return nullptr;
    }

    slang::IComponentType *components[] = {reduce.get(), reduce_element_module, texture_module, entry_point.get()};
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

template <typename T>
SlangResult encode_texture_pass(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::ITexture *source,
    u64 count,
    rhi::IBuffer *result) {

    using Info = ReduceTextureTypeInfo<T>;
    auto pipeline = get_cached_pipeline(g_buffer_pipelines, device, Info::k_pipeline_key, create_linked_texture_pipeline<T>);
    if (!pipeline) return SLANG_FAIL;

    const auto group_count = static_cast<u32>(next_reduce_count(count));
    const auto &desc = source->getDesc();
    const u32x2 source_size{desc.size.width, desc.size.height};

    auto *pass = encoder->beginComputePass();
    auto root_object = pass->bindPipeline(pipeline.get());
    auto cursor = rhi::ShaderCursor(root_object);
    const GpuSpan result_span{result->getDeviceAddress(), static_cast<u64>(group_count)};
    SLANG_RETURN_ON_FAIL(cursor["sourceSize"].setData(source_size));
    SLANG_RETURN_ON_FAIL(cursor["source"]["texture"].setBinding(source));
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
        auto session = create_linking_session(d);
        if (!session) return Slang::ComPtr<rhi::IComputePipeline>{};

        auto span = load_span_module(session.get());

        auto reduce = load_embedded_module(session.get(), EmbededModuleDesc{
                                                              .name = "reduce",
                                                              .start = _binary_reduce_slang_module_start,
                                                              .end = _binary_reduce_slang_module_end,
                                                          });

        if (!span || !reduce) return Slang::ComPtr<rhi::IComputePipeline>{};
        return create_linked_pipeline(d, session.get(), reduce.get(), Info::k_config_name, Info::k_config_source, "reduce");
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

template <typename T>
SlangResult encode_reduce_texture_sum(
    rhi::IDevice *device,
    rhi::ICommandEncoder *encoder,
    rhi::ITexture *source,
    rhi::IBuffer *result) {

    assert(device && encoder && source && result);

    using Info = ReduceTextureTypeInfo<T>;
    const auto &desc = source->getDesc();
    if (desc.type != rhi::TextureType::Texture2D) return SLANG_FAIL;
    if (desc.format != Info::k_format) return SLANG_FAIL;

    const auto count = static_cast<usize>(desc.size.width) * static_cast<usize>(desc.size.height);
    SLANG_RETURN_ON_FAIL(encode_texture_pass<T>(device, encoder, source, count, result));
    const auto reduced_count = next_reduce_count(count);
    if (reduced_count <= 1) return SLANG_OK;

    encoder->globalBarrier();
    return encode_reduce_sum<T>(device, encoder, result, reduced_count, result);
}

template <typename T>
T reduce_texture_sum(rhi::IDevice *device, rhi::ITexture *source) {
    assert(device && source);

    const auto &desc = source->getDesc();
    const auto count = static_cast<usize>(desc.size.width) * static_cast<usize>(desc.size.height);
    const auto result_size = reduce_sum_scratch_size<T>(count);
    auto result = create_buffer(
        device,
        result_size,
        rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource | rhi::BufferUsage::CopyDestination);

    auto queue = device->getQueue(rhi::QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    if (SLANG_FAILED(encode_reduce_texture_sum<T>(device, encoder.get(), source, result.get()))) {
        return {};
    }

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

#define LLC_INSTANTIATE_REDUCE_TEXTURE(T)                                                                             \
    template SlangResult encode_reduce_texture_sum<T>(                                                                \
        rhi::IDevice *, rhi::ICommandEncoder *, rhi::ITexture *, rhi::IBuffer *);                                     \
    template T reduce_texture_sum<T>(rhi::IDevice *, rhi::ITexture *);

LLC_INSTANTIATE_REDUCE(f32)
LLC_INSTANTIATE_REDUCE(f16)
LLC_INSTANTIATE_REDUCE(f32x2)
LLC_INSTANTIATE_REDUCE(f32x3)
LLC_INSTANTIATE_REDUCE(f32x4)
LLC_INSTANTIATE_REDUCE(f16x2)
LLC_INSTANTIATE_REDUCE(f16x3)
LLC_INSTANTIATE_REDUCE(f16x4)
LLC_INSTANTIATE_REDUCE_TEXTURE(f32)
LLC_INSTANTIATE_REDUCE_TEXTURE(f32x4)
// clang-format on

#undef LLC_INSTANTIATE_REDUCE
#undef LLC_INSTANTIATE_REDUCE_TEXTURE

} // namespace llc::pp
