#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <slang.h>
#include <slang-com-ptr.h>

#include <llc/context.h>
#include <llc/kernel.h>
#include <llc/types.hpp>

namespace llc {

struct App final {
    struct Config final {
        u32 iteration_count = 10000;
        u32 report_interval = 100;
        u32 random_seed = 1072;
        f32 learning_rate = 0.001f;
        std::string output_image = "brdf_2d_minimization.png";
        std::string brdf_image = "brdf_2d_params.png";
    };

    struct InputParams final {
        f32 L_x;
        f32 L_y;
        f32 L_z;
        f32 V_x;
        f32 V_y;
        f32 V_z;
    };

    struct AdamState final {
        f32 mean = 0.0f;
        f32 variance = 0.0f;
        i32 iteration = 0;
    };

    Context context_;
    Slang::ComPtr<slang::IModule> slang_module_;
    Kernel gradient_kernel_;
    Kernel adam_kernel_;
    Kernel render_kernel_;
    Config config_;

    i32 run(i32 argc, const char *argv[]);
};

} // namespace llc

