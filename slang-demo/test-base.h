#pragma once

#include "slang-com-ptr.h"
#include "slang.h"

using Slang::ComPtr;

class TestBase {

public:
    // Parses command line options. This example only has one option for testing purpose.
    int parse_option(int argc, char **argv);

    void print_entrypoint_hashes(
        int entry_point_count,
        int target_count,
        ComPtr<slang::IComponentType> &composed_program);

    bool is_test_mode() const { return is_test_mode_; }

private:
    bool is_test_mode_ = false;
    uint64_t global_counter_ = 0;
};