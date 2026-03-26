#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <span>

namespace llc {

struct Kernel final {
    Slang::ComPtr<rhi::IShaderProgram> program_;
    Slang::ComPtr<rhi::IComputePipeline> pipeline_;

    operator bool() const noexcept { return program_ && pipeline_; }
    static Kernel load(slang::IModule *slang_module,
                       rhi::IDevice *device,
                       const char *entry_point_name);
};

Slang::ComPtr<slang::IModule>
load_shader_module(slang::ISession *slang_session, const char *module_name, std::span<const char *const> extra_search_paths = {});

} // namespace llc