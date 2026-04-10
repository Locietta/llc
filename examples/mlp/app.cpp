#include "app.h"

#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>

#include <daw/json/daw_json_link.h>

#include <llc/buffer.h>
#include <llc/math.h>

namespace llc {

inline constexpr char k_json_iteration_count[] = "iteration_count";
inline constexpr char k_json_report_interval[] = "report_interval";
inline constexpr char k_json_input_count[] = "input_count";
inline constexpr char k_json_random_seed[] = "random_seed";

} // namespace llc

namespace daw::json {

template <>
struct json_data_contract<llc::App::Config> final {
    using type = json_member_list<
        json_number<llc::k_json_iteration_count, llc::u32>,
        json_number<llc::k_json_report_interval, llc::u32>,
        json_number<llc::k_json_input_count, llc::u32>,
        json_number<llc::k_json_random_seed, llc::u32>
    >;

    static inline auto to_json_data(const llc::App::Config &config) {
        return std::forward_as_tuple(
            config.iteration_count,
            config.report_interval,
            config.input_count,
            config.random_seed);
    }
};

} // namespace daw::json

namespace llc {

namespace {

using Slang::ComPtr;

constexpr std::array<u32, 3> k_layer_sizes = {4, 16, 4};
constexpr u32 k_layer_count = static_cast<u32>(k_layer_sizes.size() - 1);
constexpr char k_kernel_module_name[] = "kernels";

struct InputSample final {
    f32 x;
    f32 y;
};

struct AdamState final {
    f16 mean;
    f16 variance;
    i32 iteration;
};

template <typename T>
concept blob_compatible = standard_layout<T>;

std::string read_text_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(fmt::format("Failed to open config file: {}", path.string()));
    }

    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    std::string content(static_cast<usize>(size), '\0');
    stream.read(content.data(), size);
    if (!stream) {
        throw std::runtime_error(fmt::format("Failed to read config file: {}", path.string()));
    }
    return content;
}

std::filesystem::path resolve_input_path(const std::filesystem::path &path) {
    if (path.is_absolute() && std::filesystem::exists(path)) {
        return path;
    }

    const auto direct = std::filesystem::absolute(path);
    if (std::filesystem::exists(direct)) {
        return direct;
    }

    auto current = std::filesystem::current_path();
    while (true) {
        const auto candidate = current / path;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        if (!current.has_parent_path()) break;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    return path;
}

template <blob_compatible T>
ComPtr<rhi::IBuffer> create_device_buffer(
    Context &context,
    rhi::BufferUsage usage,
    std::span<const T> init_data = {},
    rhi::ResourceState default_state = rhi::ResourceState::UnorderedAccess) {

    return create_structured_buffer(
        context,
        init_data.size_bytes(),
        sizeof(T),
        usage,
        init_data.empty() ? nullptr : init_data.data(),
        rhi::MemoryType::DeviceLocal,
        default_state);
}

ComPtr<rhi::IBuffer> create_device_buffer(
    Context &context,
    u64 byte_size,
    u32 element_size,
    rhi::BufferUsage usage,
    const void *init_data = nullptr,
    rhi::ResourceState default_state = rhi::ResourceState::UnorderedAccess) {

    return create_structured_buffer(
        context,
        byte_size,
        element_size,
        usage,
        init_data,
        rhi::MemoryType::DeviceLocal,
        default_state);
}

usize align_up_64(usize size) noexcept {
    return divide_and_round_up(size, usize{64}) * 64;
}

u32 weight_stride(u32 layer_index) noexcept {
    return k_layer_sizes[layer_index] * sizeof(f16);
}

u32 weight_count(u32 layer_index) noexcept {
    return k_layer_sizes[layer_index] * k_layer_sizes[layer_index + 1];
}

u32 bias_count(u32 layer_index) noexcept {
    return k_layer_sizes[layer_index + 1];
}

SlangResult allocate_network_parameter_storage(
    rhi::IDevice *device,
    std::vector<App::NetworkParameterAllocation> &param_storage,
    usize &out_param_buffer_size,
    usize &out_gradient_offset,
    usize &out_gradient_training_offset) {

    out_param_buffer_size = 0;
    param_storage.clear();
    param_storage.reserve(k_layer_count);

    auto allocate_segment = [&](usize size) {
        const usize aligned_size = align_up_64(size);
        const usize offset = out_param_buffer_size;
        out_param_buffer_size += aligned_size;
        return offset;
    };

    for (u32 i = 0; i < k_layer_count; ++i) {
        App::NetworkParameterAllocation allocation;
        allocation.weights_size = weight_count(i) * sizeof(f16);
        allocation.weights_offset = allocate_segment(allocation.weights_size);
        allocation.bias_size = bias_count(i) * sizeof(f16);
        allocation.bias_offset = allocate_segment(allocation.bias_size);
        param_storage.push_back(allocation);
    }

    out_gradient_offset = out_param_buffer_size;
    for (u32 i = 0; i < k_layer_count; ++i) {
        auto &allocation = param_storage[i];
        allocation.weights_grad_offset = allocate_segment(allocation.weights_size);
        allocation.bias_grad_offset = allocate_segment(allocation.bias_size);
    }

    out_gradient_training_offset = out_param_buffer_size;
    for (u32 i = 0; i < k_layer_count; ++i) {
        auto &allocation = param_storage[i];
        usize training_size = 0;
        SLANG_RETURN_ON_FAIL(device->getCooperativeVectorMatrixSize(
            k_layer_sizes[i + 1],
            k_layer_sizes[i],
            rhi::CooperativeVectorComponentType::Float16,
            rhi::CooperativeVectorMatrixLayout::TrainingOptimal,
            0,
            &training_size));
        allocation.weights_grad_training_size = training_size;
        allocation.weights_grad_training_offset = allocate_segment(training_size);
    }

    return SLANG_OK;
}

std::vector<f16> make_initial_network_params(
    const std::vector<App::NetworkParameterAllocation> &layer_allocations,
    usize network_params_buffer_size,
    u32 random_seed) {

    std::vector<f16> init_data(network_params_buffer_size / sizeof(f16), f16{});

    std::mt19937 rng(random_seed);
    std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);

    for (const auto &allocation : layer_allocations) {
        const auto fill_range = [&](usize byte_offset, usize byte_size) {
            const usize start = byte_offset / sizeof(f16);
            const usize count = byte_size / sizeof(f16);
            for (usize i = 0; i < count; ++i) {
                init_data[start + i] = f16(dist(rng));
            }
        };

        fill_range(allocation.weights_offset, allocation.weights_size);
        fill_range(allocation.bias_offset, allocation.bias_size);
    }

    return init_data;
}

std::vector<u64> make_network_constant_buffer_data(
    rhi::IBuffer *network_params_buffer,
    const std::vector<App::NetworkParameterAllocation> &layer_allocations) {

    std::vector<u64> data;
    data.reserve(layer_allocations.size() * 4);

    const u64 base_address = network_params_buffer->getDeviceAddress();
    for (const auto &allocation : layer_allocations) {
        data.push_back(base_address + allocation.weights_offset);
        data.push_back(base_address + allocation.weights_grad_training_offset);
        data.push_back(base_address + allocation.bias_offset);
        data.push_back(base_address + allocation.bias_grad_offset);
    }

    return data;
}

std::vector<InputSample> make_input_samples(u32 input_count, u32 random_seed) {
    std::vector<InputSample> samples(input_count);
    std::mt19937 rng(random_seed ^ 0x9e3779b9u);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    for (auto &sample : samples) {
        sample.x = dist(rng);
        sample.y = dist(rng);
    }
    return samples;
}

struct TrainingBuffers final {
    ComPtr<rhi::IBuffer> network_params;
    ComPtr<rhi::IBuffer> adam_state;
    ComPtr<rhi::IBuffer> network_constants;
    ComPtr<rhi::IBuffer> input_samples;
    ComPtr<rhi::IBuffer> loss;
};

TrainingBuffers create_training_buffers(
    App &app,
    std::span<const InputSample> input_samples) {

    const auto init_params = make_initial_network_params(
        app.layer_allocations_,
        app.network_params_buffer_size_,
        app.config_.random_seed);

    TrainingBuffers buffers;
    const auto common_usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess |
                              rhi::BufferUsage::CopyDestination | rhi::BufferUsage::CopySource;

    buffers.network_params = create_device_buffer(
        app.context_,
        common_usage,
        std::span<const f16>(init_params));

    std::vector<AdamState> adam_states(app.network_gradient_training_offset_ / sizeof(f16));
    buffers.adam_state = create_device_buffer(
        app.context_,
        common_usage,
        std::span<const AdamState>(adam_states));

    const auto network_constant_data =
        make_network_constant_buffer_data(buffers.network_params.get(), app.layer_allocations_);
    buffers.network_constants = create_device_buffer(
        app.context_,
        common_usage,
        std::span<const u64>(network_constant_data));

    buffers.input_samples = create_device_buffer(app.context_, common_usage, input_samples);
    buffers.loss = create_device_buffer(app.context_, sizeof(u32), sizeof(u32), common_usage);

    return buffers;
}

SlangResult bind_pointer_uniform(
    rhi::ShaderCursor cursor,
    const char *name,
    u64 device_address) {
    return cursor[name].setData(device_address);
}

SlangResult bind_scalar_uniform(
    rhi::ShaderCursor cursor,
    const char *name,
    u32 value) {
    return cursor[name].setData(value);
}

SlangResult dispatch_learn_gradient(
    App &app,
    rhi::IBuffer *network_constants,
    rhi::IBuffer *loss_buffer,
    rhi::IBuffer *input_buffer,
    u32 sample_count) {

    auto queue = app.context_.queue();
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    {
        auto root_object = pass->bindPipeline(app.learn_grad_kernel_.pipeline_.get());
        rhi::ShaderCursor entry_cursor(root_object->getEntryPoint(0));

        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "network", network_constants->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "lossBuffer", loss_buffer->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "inputs", input_buffer->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_scalar_uniform(entry_cursor, "count", sample_count));

        pass->dispatchCompute(divide_and_round_up(sample_count, 256u), 1, 1);
    }
    pass->end();

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    return SLANG_OK;
}

SlangResult convert_weight_gradients(
    App &app,
    rhi::IBuffer *network_params_buffer) {

    std::vector<rhi::CooperativeVectorMatrixDesc> src_descs;
    std::vector<rhi::CooperativeVectorMatrixDesc> dst_descs;
    src_descs.reserve(app.layer_allocations_.size());
    dst_descs.reserve(app.layer_allocations_.size());

    for (u32 i = 0; i < app.layer_allocations_.size(); ++i) {
        const auto &allocation = app.layer_allocations_[i];

        src_descs.push_back({
            .rowCount = k_layer_sizes[i + 1],
            .colCount = k_layer_sizes[i],
            .componentType = rhi::CooperativeVectorComponentType::Float16,
            .layout = rhi::CooperativeVectorMatrixLayout::TrainingOptimal,
            .size = allocation.weights_grad_training_size,
            .offset = allocation.weights_grad_training_offset,
            .rowColumnStride = 0,
        });

        dst_descs.push_back({
            .rowCount = k_layer_sizes[i + 1],
            .colCount = k_layer_sizes[i],
            .componentType = rhi::CooperativeVectorComponentType::Float16,
            .layout = rhi::CooperativeVectorMatrixLayout::RowMajor,
            .size = allocation.weights_size,
            .offset = allocation.weights_grad_offset,
            .rowColumnStride = weight_stride(i),
        });
    }

    auto queue = app.context_.queue();
    auto encoder = queue->createCommandEncoder();
    encoder->convertCooperativeVectorMatrix(
        network_params_buffer,
        dst_descs.data(),
        network_params_buffer,
        src_descs.data(),
        static_cast<u32>(src_descs.size()));

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    return SLANG_OK;
}

SlangResult dispatch_adjust_parameters(
    App &app,
    rhi::IBuffer *adam_state,
    rhi::IBuffer *network_params,
    u32 gradient_count) {

    auto queue = app.context_.queue();
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    {
        auto root_object = pass->bindPipeline(app.adjust_parameters_kernel_.pipeline_.get());
        rhi::ShaderCursor entry_cursor(root_object->getEntryPoint(0));

        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "states", adam_state->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "params", network_params->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(
            entry_cursor,
            "gradients",
            network_params->getDeviceAddress() + app.network_gradient_offset_));
        SLANG_RETURN_ON_FAIL(bind_scalar_uniform(entry_cursor, "count", gradient_count));

        pass->dispatchCompute(divide_and_round_up(gradient_count, 256u), 1, 1);
    }
    pass->end();

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    return SLANG_OK;
}

std::optional<f32> read_loss_value(Context &context, rhi::IBuffer *loss_buffer) {
    auto loss_bits = read_buffer<u32>(context, loss_buffer, 0, 1);
    if (!loss_bits) return std::nullopt;
    return std::bit_cast<f32>(loss_bits[0]);
}

App::Config load_config(i32 argc, const char *argv[]) {
    App::Config config;
    if (argc == 1) return config;
    if (argc != 2) {
        throw std::runtime_error("Usage: xmake run mlp [config.json]");
    }

    const std::filesystem::path config_path = resolve_input_path(argv[1]);
    const auto json = read_text_file(config_path);
    config = daw::json::from_json<App::Config>(json);

    if (config.iteration_count == 0) {
        throw std::runtime_error("Config field `iteration_count` must be greater than zero.");
    }
    if (config.input_count == 0) {
        throw std::runtime_error("Config field `input_count` must be greater than zero.");
    }
    return config;
}

} // namespace

i32 App::run(i32 argc, const char *argv[]) {
    try {
        config_ = load_config(argc, argv);
    } catch (const std::exception &e) {
        fmt::println("{}", e.what());
        return -1;
    }

    rhi::DeviceDesc device_desc;
    device_desc.slang.targetProfile = "spirv_1_6";
    device_desc.deviceType = rhi::DeviceType::Vulkan;

    static constexpr slang::PreprocessorMacroDesc defines[] = {
        {"__SLANG_APPLE__", "0"},
    };
    device_desc.slang.preprocessorMacros = defines;
    device_desc.slang.preprocessorMacroCount = std::size(defines);

    auto context = Context::create(ContextDesc{.device = device_desc});
    if (!context) {
        fmt::println("Failed to create Vulkan device.");
        return -1;
    }
    context_ = std::move(*context);
    auto *device = context_.device();
    if (!device->hasFeature(rhi::Feature::CooperativeVector)) {
        fmt::println("The selected device does not support cooperative vectors.");
        return -1;
    }

    slang_module_ = load_shader_module(context_, k_kernel_module_name);
    if (!slang_module_) {
        fmt::println("Failed to load shader module: {}", k_kernel_module_name);
        return -1;
    }

    learn_grad_kernel_ = Kernel::load(slang_module_.get(), context_, "learnGradient");
    adjust_parameters_kernel_ = Kernel::load(slang_module_.get(), context_, "adjustParameters");
    if (!learn_grad_kernel_ || !adjust_parameters_kernel_) {
        fmt::println("Failed to load MLP kernels.");
        return -1;
    }

    if (SLANG_FAILED(allocate_network_parameter_storage(
            device,
            layer_allocations_,
            network_params_buffer_size_,
            network_gradient_offset_,
            network_gradient_training_offset_))) {
        fmt::println("Failed to allocate network parameter storage.");
        return -1;
    }

    const auto input_samples = make_input_samples(config_.input_count, config_.random_seed);
    auto buffers = create_training_buffers(*this, input_samples);
    if (!buffers.network_params || !buffers.adam_state || !buffers.network_constants || !buffers.input_samples ||
        !buffers.loss) {
        fmt::println("Failed to create training buffers.");
        return -1;
    }

    const u32 gradient_count =
        static_cast<u32>((network_gradient_training_offset_ - network_gradient_offset_) / sizeof(f16));

    auto queue = context_.queue();
    for (u32 iteration = 0; iteration < config_.iteration_count; ++iteration) {
        llc::clear_buffer(context_, buffers.loss.get());
        llc::clear_buffer(
            context_,
            buffers.network_params.get(),
            rhi::BufferRange{
                network_gradient_training_offset_,
                network_params_buffer_size_ - network_gradient_training_offset_,
            });

        if (SLANG_FAILED(dispatch_learn_gradient(
                *this,
                buffers.network_constants.get(),
                buffers.loss.get(),
                buffers.input_samples.get(),
                static_cast<u32>(input_samples.size())))) {
            fmt::println("Failed to dispatch `learnGradient`.");
            return -1;
        }

        if (SLANG_FAILED(convert_weight_gradients(*this, buffers.network_params.get()))) {
            fmt::println("Failed to convert cooperative vector gradients.");
            return -1;
        }

        if (SLANG_FAILED(dispatch_adjust_parameters(
                *this,
                buffers.adam_state.get(),
                buffers.network_params.get(),
                gradient_count))) {
            fmt::println("Failed to dispatch `adjustParameters`.");
            return -1;
        }

        if (config_.report_interval != 0 && ((iteration + 1) % config_.report_interval == 0)) {
            queue->waitOnHost();
            const auto loss = read_loss_value(context_, buffers.loss.get());
            if (!loss) {
                fmt::println("Failed to read back loss buffer.");
                return -1;
            }
            fmt::println("Loss after {} iterations: {}", iteration + 1, *loss);
        }
    }

    queue->waitOnHost();
    const auto final_loss = read_loss_value(context_, buffers.loss.get());
    if (!final_loss) {
        fmt::println("Failed to read back final loss.");
        return -1;
    }
    fmt::println("Final loss: {}", *final_loss);
    return 0;
}

} // namespace llc
