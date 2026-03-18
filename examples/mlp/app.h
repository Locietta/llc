#pragma once

#include <filesystem>
#include <vector>

#include <slang-com-ptr.h>
#include <slang-rhi.h>
#include <slang.h>

#include <llc/kernel.h>
#include <llc/types.hpp>

namespace llc {

struct App final {
    struct Config final {
        u32 iteration_count = 1000;
        u32 report_interval = 10;
        u32 input_count = 32;
        u32 random_seed = 1072;
    };

    struct NetworkParameterAllocation final {
        usize weights_offset = 0;
        usize weights_size = 0;
        usize bias_offset = 0;
        usize bias_size = 0;
        usize weights_grad_offset = 0;
        usize bias_grad_offset = 0;
        usize weights_grad_training_offset = 0;
        usize weights_grad_training_size = 0;
    };

    Slang::ComPtr<rhi::IDevice> device_;
    Slang::ComPtr<slang::ISession> slang_session_;
    Slang::ComPtr<slang::IModule> slang_module_;

    Kernel learn_grad_kernel_;
    Kernel adjust_parameters_kernel_;

    Config config_;

    std::vector<NetworkParameterAllocation> layer_allocations_;
    usize network_params_buffer_size_ = 0;
    usize network_gradient_offset_ = 0;
    usize network_gradient_training_offset_ = 0;

    i32 run(i32 argc, const char *argv[]);
};

} // namespace llc
