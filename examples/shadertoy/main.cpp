#include "app.h"

#include <cstdlib>

#include <fmt/core.h>

namespace {

constexpr llc::f32 k_default_time_seconds = 3.0f;

} // namespace

llc::i32 main(llc::i32 argc, char **argv) {
    llc::f32 time_seconds = k_default_time_seconds;
    if (argc > 1) {
        char *end = nullptr;
        time_seconds = std::strtof(argv[1], &end);
        if (end == argv[1] || (end && *end != '\0')) {
            fmt::println("Invalid time value '{}'. Expected a float in seconds.", argv[1]);
            return 1;
        }
    }

    llc::App app;
    return app.run(time_seconds);
}
