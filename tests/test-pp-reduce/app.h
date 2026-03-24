#pragma once

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <llc/types.hpp>

namespace llc {

struct App final {
    Slang::ComPtr<rhi::IDevice> device_;

    i32 run(i32 argc, const char *argv[]);
};

} // namespace llc
