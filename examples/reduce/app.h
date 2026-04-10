#pragma once

#include <llc/context.h>
#include <llc/types.hpp>


namespace llc {

struct App final {
    Context context_;

    i32 run(i32 argc, const char *argv[]);
};

} // namespace llc
