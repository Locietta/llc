#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <span>

#include <llc/context.h>

namespace llc {

struct Kernel final {
    Slang::ComPtr<rhi::IShaderProgram> program_;
    Slang::ComPtr<rhi::IComputePipeline> pipeline_;

    operator bool() const noexcept { return program_ && pipeline_; }
    static Kernel load(slang::IModule *slang_module, Context &context, const char *entry_point_name);
};

Slang::ComPtr<slang::IModule>
load_shader_module(Context &context, const char *module_name, std::span<const char *const> extra_search_paths = {});

} // namespace llc
