#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <llc/types.hpp>
#include <llc/kernel.h>

namespace llc {

struct App final {
    Slang::ComPtr<rhi::IDevice> device_;
    Slang::ComPtr<slang::ISession> slang_session_;
    Slang::ComPtr<slang::IModule> slang_module_;

    Kernel naive_kernel_;

    i32 run(i32 argc, const char *argv[]);
};

} // namespace llc