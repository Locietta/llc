#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>
#include <slang.h>

#include <llc/kernel.h>
#include <llc/types.hpp>

namespace llc {

struct App final {
    Slang::ComPtr<rhi::IDevice> device_;
    Slang::ComPtr<slang::ISession> slang_session_;
    Slang::ComPtr<slang::IModule> slang_module_;

    Kernel shader_kernel_;

    i32 run(float time_seconds);
};

} // namespace llc
