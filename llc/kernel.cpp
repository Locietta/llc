#include "kernel.h"

#include <array>
#include <filesystem>
#include <ranges>
#include <span>
#include <vector>

#include <llc/blob.h>

namespace llc {

using Slang::ComPtr;
using std::filesystem::path;

ComPtr<slang::IModule> load_shader_module(
    slang::ISession *slang_session,
    const char *module_name,
    std::span<const char *const> extra_search_paths) {

    ComPtr<slang::IModule> slang_module;
    ComPtr<slang::IBlob> diagnostics;

    // // get extension of the filename
    // const auto extension = std::filesystem::path(filename).extension().string();
    // // check if the file is binary module
    // const bool is_binary_module = extension == ".slang-module" || extension == ".slang-lib";

    constexpr auto default_search_paths = std::array{
        ".",
        "./shaders",
        "./assets/shaders",
    };
    const auto default_paths = std::span<const char *const>{default_search_paths};

    const path binary_module_filename = (path(module_name) += ".slang-module");
    const path source_module_filename = (path(module_name) += ".slang");

    for (const auto &search_path : std::array{extra_search_paths, default_paths} | std::views::join) {
        const path current_search_path = path(search_path);
        
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
    rhi::IDevice *device,
    const char *entry_point_name) {

    ComPtr<slang::IEntryPoint> entry_point;
    slang_module->findEntryPointByName(entry_point_name, entry_point.writeRef());

    ComPtr<slang::IComponentType> linked_program;
    entry_point->link(linked_program.writeRef());

    Kernel res;

    rhi::ComputePipelineDesc desc;
    auto program = device->createShaderProgram(linked_program);
    desc.program = program.get();
    res.program_ = program;
    res.pipeline_ = device->createComputePipeline(desc);
    return res;
}

} // namespace llc