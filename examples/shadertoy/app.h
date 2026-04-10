#pragma once

#include <slang.h>

#include <llc/context.h>
#include <llc/kernel.h>
#include <llc/types.hpp>

namespace llc {

struct App final {
    Context context_;
    Slang::ComPtr<slang::IModule> slang_module_;

    Kernel shader_kernel_;

    i32 run(f32 time_seconds);
};

} // namespace llc
