#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <llc/types.hpp>


namespace llc {

struct App final {
    Slang::ComPtr<rhi::IDevice> device_;
    Slang::ComPtr<slang::ISession> slang_session_;

    i32 run(i32 argc, const char *argv[]);
};

} // namespace llc