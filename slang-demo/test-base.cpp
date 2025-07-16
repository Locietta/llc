#include "test-base.h"
#include <fmt/format.h>
#include <iterator>
#include <string>

#ifdef _WIN32
// clang-format off
// include ordering sensitive
#    include <windows.h>
#    include <shellapi.h>
// clang-format on
#endif

int TestBase::parse_option(int argc, char **argv) {
    // We only make the parse in a very loose way for only extracting the test option.
#ifdef _WIN32
    wchar_t **w_argv;
    w_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#endif

    for (int i = 0; i < argc; i++) {
#ifdef _WIN32
        if (wcscmp(w_argv[i], L"--test-mode") == 0)
#else
        if (strcmp(argv[i], "--test-mode") == 0)
#endif
        {
            is_test_mode_ = true;
        }
    }

#ifdef _WIN32
    LocalFree(w_argv);
#endif

    return 0;
}

void TestBase::print_entrypoint_hashes(
    int entry_point_count,
    int target_count,
    ComPtr<slang::IComponentType> &composed_program) {
    for (int target_index = 0; target_index < target_count; target_index++) {
        for (int entry_point_index = 0; entry_point_index < entry_point_count; entry_point_index++) {
            ComPtr<slang::IBlob> entry_point_hash_blob;
            composed_program->getEntryPointHash(
                entry_point_index,
                target_index,
                entry_point_hash_blob.writeRef());

            std::string out_str = fmt::format("callIdx: {}, entrypoint: {}, target: {}, hash: ",
                                              global_counter_,
                                              entry_point_index,
                                              target_index);

            global_counter_++;

            uint8_t *buffer = (uint8_t *) entry_point_hash_blob->getBufferPointer();
            for (size_t i = 0; i < entry_point_hash_blob->getBufferSize(); i++) {
                fmt::format_to(std::back_inserter(out_str), "{:02X}", buffer[i]);
            }
            fprintf(stdout, "%s\n", out_str.c_str());
        }
    }
}