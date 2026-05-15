#include "app.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <daw/json/daw_json_link.h>
#include <fmt/core.h>
#include <slang-rhi/shader-cursor.h>

#include <llc/buffer.h>
#include <llc/image.h>
#include <llc/math.h>

namespace llc {

inline constexpr char k_json_iteration_count[] = "iteration_count";
inline constexpr char k_json_report_interval[] = "report_interval";
inline constexpr char k_json_random_seed[] = "random_seed";
inline constexpr char k_json_full_width[] = "full_width";
inline constexpr char k_json_full_height[] = "full_height";
inline constexpr char k_json_crop_x[] = "crop_x";
inline constexpr char k_json_crop_y[] = "crop_y";
inline constexpr char k_json_learning_rate[] = "learning_rate";
inline constexpr char k_json_output_image[] = "output_image";
inline constexpr char k_json_brdf_image[] = "brdf_image";

} // namespace llc

namespace daw::json {

template <>
struct json_data_contract<llc::App::Config> final {
    using type = json_member_list<
        json_number<llc::k_json_iteration_count, llc::u32>,
        json_number<llc::k_json_report_interval, llc::u32>,
        json_number<llc::k_json_random_seed, llc::u32>,
        json_number<llc::k_json_full_width, llc::u32>,
        json_number<llc::k_json_full_height, llc::u32>,
        json_number<llc::k_json_crop_x, llc::u32>,
        json_number<llc::k_json_crop_y, llc::u32>,
        json_number<llc::k_json_learning_rate, llc::f32>,
        json_string<llc::k_json_output_image, std::string>,
        json_string<llc::k_json_brdf_image, std::string>
    >;

    static inline auto to_json_data(const llc::App::Config &config) {
        return std::forward_as_tuple(
            config.iteration_count,
            config.report_interval,
            config.random_seed,
            config.full_width,
            config.full_height,
            config.crop_x,
            config.crop_y,
            config.learning_rate,
            config.output_image,
            config.brdf_image);
    }
};

} // namespace daw::json

namespace llc {

namespace {

using Slang::ComPtr;

constexpr char k_shader_module_name[] = "brdf_2d";
constexpr u32 k_channel_count = 7;
constexpr u32 k_render_channel_count = 3;
constexpr u32 k_group_size_1d = 256;
constexpr u32 k_group_size_2d = 16;
constexpr App::InputParams k_eval_params{0.2f, -0.2f, 0.6f, 0.0f, 0.0f, 1.0f};

struct Dimensions final {
    u32 full_width = 0;
    u32 full_height = 0;
    u32 half_width = 0;
    u32 half_height = 0;
    u32 parameter_count = 0;

    explicit Dimensions(const App::Config &config)
        : full_width(config.full_width),
          full_height(config.full_height),
          half_width(config.full_width / 2),
          half_height(config.full_height / 2),
          parameter_count(half_width * half_height * k_channel_count) {}
};

struct TrainingBuffers final {
    ComPtr<rhi::IBuffer> full_brdf;
    ComPtr<rhi::IBuffer> half_brdf;
    ComPtr<rhi::IBuffer> half_brdf_initial;
    ComPtr<rhi::IBuffer> gradients;
    ComPtr<rhi::IBuffer> adam_state;
    ComPtr<rhi::IBuffer> render_reference;
    ComPtr<rhi::IBuffer> render_initial;
    ComPtr<rhi::IBuffer> render_optimized;
};

struct LoadedRgbImage final {
    i32 width = 0;
    i32 height = 0;
    std::vector<u8> pixels;

    const u8 *pixel(i32 x, i32 y) const noexcept {
        return pixels.data() + (static_cast<usize>(y) * width + x) * 3;
    }
};

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

std::filesystem::path example_root() {
    const auto marker = std::filesystem::path("examples") / "brdf_2d_minimization" / "resources" / "diffuse.jpg";
    const auto resolved = resolve_input_path(marker);
    if (std::filesystem::exists(resolved)) {
        return resolved.parent_path().parent_path();
    }
    return std::filesystem::path("examples") / "brdf_2d_minimization";
}

LoadedRgbImage load_rgb_image(const std::filesystem::path &path) {
    i32 width = 0;
    i32 height = 0;
    i32 channels = 0;
    stbi_uc *data = stbi_load(path.string().c_str(), &width, &height, &channels, 3);
    if (!data) {
        throw std::runtime_error(fmt::format("Failed to load image: {}", path.string()));
    }

    LoadedRgbImage image;
    image.width = width;
    image.height = height;
    image.pixels.assign(data, data + static_cast<usize>(width) * height * 3);
    stbi_image_free(data);
    return image;
}

f32 image_channel_bgr_to_rgb(const LoadedRgbImage &image, i32 x, i32 y, u32 rgb_channel) {
    const u8 *pixel = image.pixel(x, y);
    return static_cast<f32>(pixel[rgb_channel]) / 255.0f;
}

std::vector<f32> make_full_brdf(const std::filesystem::path &root, const App::Config &config, const Dimensions &dims) {
    const auto resource_dir = root / "resources";
    auto diffuse = load_rgb_image(resource_dir / "diffuse.jpg");
    auto normal = load_rgb_image(resource_dir / "normal.jpg");
    auto roughness = load_rgb_image(resource_dir / "roughness.jpg");

    const i32 crop_x = static_cast<i32>(config.crop_x);
    const i32 crop_y = static_cast<i32>(config.crop_y);
    if (diffuse.width < crop_x + static_cast<i32>(dims.full_width) ||
        diffuse.height < crop_y + static_cast<i32>(dims.full_height) ||
        normal.width < crop_x + static_cast<i32>(dims.full_width) ||
        normal.height < crop_y + static_cast<i32>(dims.full_height) ||
        roughness.width < crop_x + static_cast<i32>(dims.full_width) ||
        roughness.height < crop_y + static_cast<i32>(dims.full_height)) {
        throw std::runtime_error(fmt::format(
            "Input resources are smaller than the expected {}x{} crop at ({}, {}).",
            dims.full_width,
            dims.full_height,
            config.crop_x,
            config.crop_y));
    }

    std::vector<f32> brdf(static_cast<usize>(dims.full_width) * dims.full_height * k_channel_count);
    for (u32 y = 0; y < dims.full_height; ++y) {
        for (u32 x = 0; x < dims.full_width; ++x) {
            const i32 sx = crop_x + static_cast<i32>(x);
            const i32 sy = crop_y + static_cast<i32>(y);
            const usize offset = (static_cast<usize>(y) * dims.full_width + x) * k_channel_count;
            brdf[offset + 0] = image_channel_bgr_to_rgb(diffuse, sx, sy, 0);
            brdf[offset + 1] = image_channel_bgr_to_rgb(diffuse, sx, sy, 1);
            brdf[offset + 2] = image_channel_bgr_to_rgb(diffuse, sx, sy, 2);
            brdf[offset + 3] = image_channel_bgr_to_rgb(normal, sx, sy, 0);
            brdf[offset + 4] = image_channel_bgr_to_rgb(normal, sx, sy, 1);
            brdf[offset + 5] = image_channel_bgr_to_rgb(normal, sx, sy, 2);
            const f32 r = image_channel_bgr_to_rgb(roughness, sx, sy, 0);
            brdf[offset + 6] = r * r;
        }
    }
    return brdf;
}

std::vector<f32> make_half_brdf(std::span<const f32> full_brdf, const Dimensions &dims) {
    std::vector<f32> half(static_cast<usize>(dims.half_width) * dims.half_height * k_channel_count);
    for (u32 y = 0; y < dims.half_height; ++y) {
        for (u32 x = 0; x < dims.half_width; ++x) {
            for (u32 channel = 0; channel < k_channel_count; ++channel) {
                f32 sum = 0.0f;
                for (u32 dy = 0; dy < 2; ++dy) {
                    for (u32 dx = 0; dx < 2; ++dx) {
                        const u32 sx = x * 2 + dx;
                        const u32 sy = y * 2 + dy;
                        sum += full_brdf[(static_cast<usize>(sy) * dims.full_width + sx) * k_channel_count + channel];
                    }
                }
                half[(static_cast<usize>(y) * dims.half_width + x) * k_channel_count + channel] = sum * 0.25f;
            }
        }
    }
    return half;
}

template <standard_layout T>
ComPtr<rhi::IBuffer> create_device_buffer(Context &context, rhi::BufferUsage usage, std::span<const T> init_data) {
    return create_structured_buffer(
        context,
        init_data.size_bytes(),
        sizeof(T),
        usage,
        init_data.empty() ? nullptr : init_data.data(),
        rhi::MemoryType::DeviceLocal,
        rhi::ResourceState::UnorderedAccess);
}

ComPtr<rhi::IBuffer> create_device_buffer(Context &context, u64 byte_size, u32 element_size, rhi::BufferUsage usage) {
    return create_structured_buffer(
        context,
        byte_size,
        element_size,
        usage,
        nullptr,
        rhi::MemoryType::DeviceLocal,
        rhi::ResourceState::UnorderedAccess);
}

TrainingBuffers create_training_buffers(
    Context &context,
    const Dimensions &dims,
    std::span<const f32> full_brdf,
    std::span<const f32> half_brdf) {
    const auto usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopySource |
                       rhi::BufferUsage::CopyDestination | rhi::BufferUsage::UnorderedAccess;

    TrainingBuffers buffers;
    buffers.full_brdf = create_device_buffer(context, usage, full_brdf);
    buffers.half_brdf = create_device_buffer(context, usage, half_brdf);
    buffers.half_brdf_initial = create_device_buffer(context, usage, half_brdf);
    buffers.gradients = create_device_buffer(context, sizeof(f32) * dims.parameter_count, sizeof(f32), usage);
    std::vector<App::AdamState> states(dims.parameter_count);
    buffers.adam_state = create_device_buffer(context, usage, std::span<const App::AdamState>(states));
    buffers.render_reference = create_device_buffer(
        context,
        sizeof(f32) * dims.full_width * dims.full_height * k_render_channel_count,
        sizeof(f32),
        usage);
    buffers.render_initial = create_device_buffer(
        context,
        sizeof(f32) * dims.full_width * dims.full_height * k_render_channel_count,
        sizeof(f32),
        usage);
    buffers.render_optimized = create_device_buffer(
        context,
        sizeof(f32) * dims.full_width * dims.full_height * k_render_channel_count,
        sizeof(f32),
        usage);
    return buffers;
}

SlangResult bind_pointer_uniform(rhi::ShaderCursor cursor, const char *name, u64 device_address) {
    return cursor[name].setData(device_address);
}

SlangResult bind_u32_uniform(rhi::ShaderCursor cursor, const char *name, u32 value) {
    return cursor[name].setData(value);
}

SlangResult bind_f32_uniform(rhi::ShaderCursor cursor, const char *name, f32 value) {
    return cursor[name].setData(value);
}

SlangResult bind_input_params(rhi::ShaderCursor cursor, const App::InputParams &value) {
    return cursor["inputParams"].setData(value);
}

SlangResult dispatch_gradient(App &app, TrainingBuffers &buffers, const Dimensions &dims, const App::InputParams &params) {
    auto queue = app.context_.queue();
    auto encoder = queue->createCommandEncoder();
    auto *pass = encoder->beginComputePass();
    {
        auto root_object = pass->bindPipeline(app.gradient_kernel_.pipeline_.get());
        rhi::ShaderCursor entry_cursor(root_object->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "fullBrdf", buffers.full_brdf->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "halfBrdf", buffers.half_brdf->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "gradients", buffers.gradients->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "parameterCount", dims.parameter_count));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "fullWidth", dims.full_width));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "fullHeight", dims.full_height));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "halfWidth", dims.half_width));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "halfHeight", dims.half_height));
        SLANG_RETURN_ON_FAIL(bind_input_params(entry_cursor, params));
        pass->dispatchCompute(dims.parameter_count, 1, 1);
    }
    pass->end();

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    return SLANG_OK;
}

SlangResult dispatch_adam(App &app, TrainingBuffers &buffers, const Dimensions &dims) {
    auto queue = app.context_.queue();
    auto encoder = queue->createCommandEncoder();
    auto *pass = encoder->beginComputePass();
    {
        auto root_object = pass->bindPipeline(app.adam_kernel_.pipeline_.get());
        rhi::ShaderCursor entry_cursor(root_object->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "states", buffers.adam_state->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "params", buffers.half_brdf->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "gradients", buffers.gradients->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "parameterCount", dims.parameter_count));
        SLANG_RETURN_ON_FAIL(bind_f32_uniform(entry_cursor, "learningRate", app.config_.learning_rate));
        pass->dispatchCompute(divide_and_round_up(dims.parameter_count, k_group_size_1d), 1, 1);
    }
    pass->end();

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    return SLANG_OK;
}

SlangResult dispatch_render(
    App &app,
    rhi::IBuffer *brdf,
    rhi::IBuffer *output,
    const Dimensions &dims,
    u32 brdf_width,
    u32 brdf_height,
    const App::InputParams &params) {
    auto queue = app.context_.queue();
    auto encoder = queue->createCommandEncoder();
    auto *pass = encoder->beginComputePass();
    {
        auto root_object = pass->bindPipeline(app.render_kernel_.pipeline_.get());
        rhi::ShaderCursor entry_cursor(root_object->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "brdf", brdf->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_pointer_uniform(entry_cursor, "output", output->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "outputWidth", dims.full_width));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "outputHeight", dims.full_height));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "brdfWidth", brdf_width));
        SLANG_RETURN_ON_FAIL(bind_u32_uniform(entry_cursor, "brdfHeight", brdf_height));
        SLANG_RETURN_ON_FAIL(bind_input_params(entry_cursor, params));
        pass->dispatchCompute(
            divide_and_round_up(dims.full_width, k_group_size_2d),
            divide_and_round_up(dims.full_height, k_group_size_2d),
            1);
    }
    pass->end();

    ComPtr<rhi::ICommandBuffer> command_buffer;
    SLANG_RETURN_ON_FAIL(encoder->finish(command_buffer.writeRef()));
    queue->submit(command_buffer.get());
    return SLANG_OK;
}

f32 mean_squared_error(std::span<const f32> reference, std::span<const f32> candidate, const Dimensions &dims) {
    f64 sum = 0.0;
    for (usize i = 0; i < reference.size(); ++i) {
        const f64 diff = static_cast<f64>(candidate[i]) - reference[i];
        sum += diff * diff;
    }
    return static_cast<f32>(sum / (static_cast<f64>(dims.full_width) * dims.full_height));
}

u8 float_to_unorm8(f32 value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<u8>(value * 255.0f + 0.5f);
}

void store_rgba(Image &image, u32 x, u32 y, f32 r, f32 g, f32 b) {
    auto *dst = image.row_data(y) + static_cast<usize>(x) * 4;
    dst[0] = static_cast<byte>(float_to_unorm8(r));
    dst[1] = static_cast<byte>(float_to_unorm8(g));
    dst[2] = static_cast<byte>(float_to_unorm8(b));
    dst[3] = static_cast<byte>(255);
}

void blit_render(
    Image &image,
    std::span<const f32> render,
    const Dimensions &dims,
    u32 dst_x,
    u32 dst_y,
    f32 scale = 2.0f) {
    for (u32 y = 0; y < dims.full_height; ++y) {
        for (u32 x = 0; x < dims.full_width; ++x) {
            const usize offset = (static_cast<usize>(y) * dims.full_width + x) * k_render_channel_count;
            store_rgba(
                image,
                dst_x + x,
                dst_y + y,
                render[offset + 0] * scale,
                render[offset + 1] * scale,
                render[offset + 2] * scale);
        }
    }
}

Image make_comparison_image(
    std::span<const f32> initial,
    std::span<const f32> optimized,
    std::span<const f32> reference,
    const Dimensions &dims) {
    const u32 output_width = dims.full_width * 2;
    const u32 output_height = dims.full_height * 2;
    Image image(output_width, output_height, rhi::Format::RGBA8Unorm, output_width * 4);
    blit_render(image, initial, dims, 0, 0);
    blit_render(image, optimized, dims, dims.full_width, 0);
    blit_render(image, reference, dims, 0, dims.full_height);

    for (u32 y = 0; y < dims.full_height; ++y) {
        for (u32 x = 0; x < dims.full_width; ++x) {
            const usize offset = (static_cast<usize>(y) * dims.full_width + x) * k_render_channel_count;
            store_rgba(
                image,
                dims.full_width + x,
                dims.full_height + y,
                0.5f * std::abs(reference[offset + 0] - optimized[offset + 0]),
                0.5f * std::abs(reference[offset + 1] - optimized[offset + 1]),
                0.5f * std::abs(reference[offset + 2] - optimized[offset + 2]));
        }
    }
    return image;
}

Image make_brdf_sheet(std::span<const f32> initial, std::span<const f32> optimized, const Dimensions &dims) {
    const u32 brdf_sheet_width = dims.half_width * 3;
    const u32 brdf_sheet_height = dims.half_height * 2;
    Image image(brdf_sheet_width, brdf_sheet_height, rhi::Format::RGBA8Unorm, brdf_sheet_width * 4);
    auto blit = [&](std::span<const f32> brdf, u32 dst_x, u32 dst_y, u32 channel_base, bool grayscale) {
        for (u32 y = 0; y < dims.half_height; ++y) {
            for (u32 x = 0; x < dims.half_width; ++x) {
                const usize offset = (static_cast<usize>(y) * dims.half_width + x) * k_channel_count;
                if (grayscale) {
                    const f32 v = brdf[offset + channel_base];
                    store_rgba(image, dst_x + x, dst_y + y, v, v, v);
                } else {
                    store_rgba(
                        image,
                        dst_x + x,
                        dst_y + y,
                        brdf[offset + channel_base + 0],
                        brdf[offset + channel_base + 1],
                        brdf[offset + channel_base + 2]);
                }
            }
        }
    };

    blit(initial, 0, 0, 0, false);
    blit(initial, dims.half_width, 0, 3, false);
    blit(initial, dims.half_width * 2, 0, 6, true);
    blit(optimized, 0, dims.half_height, 0, false);
    blit(optimized, dims.half_width, dims.half_height, 3, false);
    blit(optimized, dims.half_width * 2, dims.half_height, 6, true);
    return image;
}

App::Config load_config(i32 argc, const char *argv[]) {
    App::Config config;
    if (argc == 1) return config;
    if (argc != 2) {
        throw std::runtime_error("Usage: xmake run brdf_2d_minimization [config.json]");
    }

    const auto config_path = resolve_input_path(argv[1]);
    const auto json = read_text_file(config_path);
    config = daw::json::from_json<App::Config>(json);
    if (config.iteration_count == 0) {
        throw std::runtime_error("Config field `iteration_count` must be greater than zero.");
    }
    if (config.learning_rate <= 0.0f) {
        throw std::runtime_error("Config field `learning_rate` must be greater than zero.");
    }
    if (config.full_width == 0 || config.full_height == 0) {
        throw std::runtime_error("Config fields `full_width` and `full_height` must be greater than zero.");
    }
    if ((config.full_width % 2) != 0 || (config.full_height % 2) != 0) {
        throw std::runtime_error("Config fields `full_width` and `full_height` must be even for 2x downsampling.");
    }
    return config;
}

App::InputParams random_training_params(std::mt19937 &rng) {
    std::normal_distribution<f32> dist(0.0f, 1.0f);
    f32 x = dist(rng);
    f32 y = dist(rng);
    f32 z = dist(rng);
    const f32 inv_len = 1.0f / (std::sqrt(x * x + y * y + z * z) + 0.0001f);
    x *= inv_len;
    y *= inv_len;
    z = std::abs(z * inv_len);
    return {x, y, z, 0.0f, 0.0f, 1.0f};
}

bool all_buffers_valid(const TrainingBuffers &buffers) {
    return buffers.full_brdf && buffers.half_brdf && buffers.half_brdf_initial && buffers.gradients &&
           buffers.adam_state && buffers.render_reference && buffers.render_initial && buffers.render_optimized;
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

    auto context = Context::create(ContextDesc{.device = device_desc});
    if (!context) {
        fmt::println("Failed to create Vulkan device.");
        return -1;
    }
    context_ = std::move(*context);

    slang_module_ = load_shader_module(context_, k_shader_module_name);
    if (!slang_module_) {
        fmt::println("Failed to load shader module: {}", k_shader_module_name);
        return -1;
    }

    gradient_kernel_ = Kernel::load(slang_module_.get(), context_, "computeGradient");
    adam_kernel_ = Kernel::load(slang_module_.get(), context_, "adamStep");
    render_kernel_ = Kernel::load(slang_module_.get(), context_, "renderBrdf");
    if (!gradient_kernel_ || !adam_kernel_ || !render_kernel_) {
        fmt::println("Failed to load BRDF kernels.");
        return -1;
    }

    const Dimensions dims(config_);
    std::vector<f32> full_brdf;
    std::vector<f32> half_brdf;
    try {
        const auto root = example_root();
        full_brdf = make_full_brdf(root, config_, dims);
        half_brdf = make_half_brdf(full_brdf, dims);
    } catch (const std::exception &e) {
        fmt::println("{}", e.what());
        return -1;
    }

    auto buffers = create_training_buffers(context_, dims, full_brdf, half_brdf);
    if (!all_buffers_valid(buffers)) {
        fmt::println("Failed to create training buffers.");
        return -1;
    }

    std::mt19937 rng(config_.random_seed);
    auto queue = context_.queue();
    for (u32 iteration = 0; iteration < config_.iteration_count; ++iteration) {
        clear_buffer(context_, buffers.gradients.get());
        const auto params = random_training_params(rng);
        if (SLANG_FAILED(dispatch_gradient(*this, buffers, dims, params))) {
            fmt::println("Failed to dispatch computeGradient.");
            return -1;
        }
        if (SLANG_FAILED(dispatch_adam(*this, buffers, dims))) {
            fmt::println("Failed to dispatch adamStep.");
            return -1;
        }

        if (config_.report_interval != 0 && ((iteration + 1) % config_.report_interval == 0)) {
            queue->waitOnHost();
            fmt::println("Iteration {}", iteration + 1);
        }
    }

    if (SLANG_FAILED(dispatch_render(
            *this,
            buffers.full_brdf.get(),
            buffers.render_reference.get(),
            dims,
            dims.full_width,
            dims.full_height,
            k_eval_params)) ||
        SLANG_FAILED(dispatch_render(
            *this,
            buffers.half_brdf_initial.get(),
            buffers.render_initial.get(),
            dims,
            dims.half_width,
            dims.half_height,
            k_eval_params)) ||
        SLANG_FAILED(dispatch_render(
            *this,
            buffers.half_brdf.get(),
            buffers.render_optimized.get(),
            dims,
            dims.half_width,
            dims.half_height,
            k_eval_params))) {
        fmt::println("Failed to render BRDF comparison images.");
        return -1;
    }
    queue->waitOnHost();

    const u32 render_value_count = dims.full_width * dims.full_height * k_render_channel_count;
    auto reference = read_buffer<f32>(context_, buffers.render_reference.get(), 0, render_value_count);
    auto initial = read_buffer<f32>(context_, buffers.render_initial.get(), 0, render_value_count);
    auto optimized = read_buffer<f32>(context_, buffers.render_optimized.get(), 0, render_value_count);
    auto optimized_brdf = read_buffer<f32>(context_, buffers.half_brdf.get(), 0, dims.parameter_count);
    if (!reference || !initial || !optimized || !optimized_brdf) {
        fmt::println("Failed to read back result buffers.");
        return -1;
    }

    const f32 initial_loss = mean_squared_error(reference.as_span(), initial.as_span(), dims);
    const f32 optimized_loss = mean_squared_error(reference.as_span(), optimized.as_span(), dims);
    fmt::println("Initial eval loss: {}", initial_loss);
    fmt::println("Optimized eval loss: {}", optimized_loss);

    auto comparison = make_comparison_image(initial.as_span(), optimized.as_span(), reference.as_span(), dims);
    if (!write_image_png(config_.output_image, comparison)) {
        fmt::println("Failed to write PNG: {}", config_.output_image);
        return -1;
    }

    auto brdf_sheet = make_brdf_sheet(half_brdf, optimized_brdf.as_span(), dims);
    if (!write_image_png(config_.brdf_image, brdf_sheet)) {
        fmt::println("Failed to write PNG: {}", config_.brdf_image);
        return -1;
    }

    fmt::println("Wrote {}", std::filesystem::absolute(config_.output_image).string());
    fmt::println("Wrote {}", std::filesystem::absolute(config_.brdf_image).string());
    return 0;
}

} // namespace llc
