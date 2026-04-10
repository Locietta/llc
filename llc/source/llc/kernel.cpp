#include "kernel.h"

#include <array>
#include <filesystem>
#include <ranges>
#include <span>
#include <system_error>

#include <llc/blob.h>
#include <llc/utils/fs.h>

namespace llc {

using Slang::ComPtr;
using std::filesystem::path;

namespace {

constexpr auto k_default_search_paths = std::array{
    ".",
    "./shaders",
    "./assets/shaders",
};

path get_search_path(std::span<const char *const> extra_search_paths, usize index) {
    if (index < extra_search_paths.size()) return path(extra_search_paths[index]);
    return path(k_default_search_paths[index - extra_search_paths.size()]);
}

auto build_search_directories(std::span<const char *const> extra_search_paths) {
    std::array<path, 2> search_roots;
    usize search_root_count = 0;

    std::error_code error_code;
    const auto cwd = std::filesystem::current_path(error_code);
    if (!error_code) search_roots[search_root_count++] = cwd;

    const auto exe_dir = executable_directory();
    if (!exe_dir.empty() && (search_root_count == 0 || exe_dir != search_roots[0])) {
        search_roots[search_root_count++] = exe_dir;
    }

    const usize search_path_count = extra_search_paths.size() + k_default_search_paths.size();
    const usize root_count = search_root_count > 0 ? search_root_count : 1;

    return std::views::iota(usize{0}, search_path_count * root_count) | std::views::transform([=](usize index) {
               if (search_root_count == 0) return get_search_path(extra_search_paths, index);

               const usize root_index = index / search_path_count;
               const usize search_path_index = index % search_path_count;
               const path search_path = get_search_path(extra_search_paths, search_path_index);
               if (search_path.is_absolute()) {
                   return root_index == 0 ? search_path : path{};
               }

               return search_roots[root_index] / search_path;
           }) |
           std::views::filter([](const path &search_directory) { return !search_directory.empty(); });
}

} // namespace

ComPtr<slang::IModule> load_shader_module(
    Context &context,
    const char *module_name,
    std::span<const char *const> extra_search_paths) {

    auto *slang_session = context.slang_session();

    ComPtr<slang::IModule> slang_module;
    ComPtr<slang::IBlob> diagnostics;

    const path binary_module_filename = (path(module_name) += ".slang-module");
    const path source_module_filename = (path(module_name) += ".slang");

    for (const auto &current_search_path : build_search_directories(extra_search_paths)) {

        // try load binary module
        do {
            const path binary_full_path = current_search_path / binary_module_filename;
            if (!std::filesystem::exists(binary_full_path)) break;

            auto shader_ir = FileBlob::load(binary_full_path);
            if (!shader_ir) break;

            slang_module = slang_session->loadModuleFromIRBlob(
                module_name,
                binary_full_path.string().c_str(),
                shader_ir.get(),
                diagnostics.writeRef());
        } while (false); /// goto, but with dijkstra's flavor

        if (slang_module) break;

        // try load source module
        const path source_full_path = current_search_path / source_module_filename;
        if (!std::filesystem::exists(source_full_path)) continue;
        slang_module = slang_session->loadModule(
            source_full_path.string().c_str(),
            diagnostics.writeRef());

        if (slang_module) break;
    }

    return slang_module;
}

Kernel Kernel::load(
    slang::IModule *slang_module,
    Context &context,
    const char *entry_point_name) {

    ComPtr<slang::IEntryPoint> entry_point;
    slang_module->findEntryPointByName(entry_point_name, entry_point.writeRef());

    ComPtr<slang::IComponentType> linked_program;
    entry_point->link(linked_program.writeRef());

    Kernel res;

    rhi::ComputePipelineDesc desc;
    auto *device = context.device();
    auto program = device->createShaderProgram(linked_program);
    desc.program = program.get();
    res.program_ = program;
    res.pipeline_ = device->createComputePipeline(desc);
    return res;
}

} // namespace llc
