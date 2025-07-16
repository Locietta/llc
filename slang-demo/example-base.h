#pragma once

#include "slang-gfx.h"
#include "test-base.h"

#include "util/platform/window.h"
#include "util/smart-pointer.h"
#include "util/types.h"

#include <fmt/base.h>
#include <string>
#include <filesystem>
#include <vector>

#ifdef _WIN32
void _Win32OutputDebugString(const char *str); // NOLINT(readability-identifier-naming)
#endif

#define SLANG_STRINGIFY(x) #x
#define SLANG_EXPAND_STRINGIFY(x) SLANG_STRINGIFY(x)

#ifdef _WIN32
#define EXAMPLE_MAIN(innerMain)                                   \
    extern const char *const g_logFileName =                      \
        "log-" SLANG_EXPAND_STRINGIFY(SLANG_EXAMPLE_NAME) ".txt"; \
    PLATFORM_UI_MAIN(innerMain);

#else
#define EXAMPLE_MAIN(innerMain) PLATFORM_UI_MAIN(innerMain)
#endif // _WIN32

struct WindowedAppBase : public TestBase {
protected:
    static const int k_k_swapchain_image_count = 2;

    loia::RefPtr<loia::platform::Window> g_window;
    uint32_t window_width;
    uint32_t window_height;

    Slang::ComPtr<gfx::IDevice> g_device;

    Slang::ComPtr<gfx::ISwapchain> g_swapchain;
    Slang::ComPtr<gfx::IFramebufferLayout> g_framebuffer_layout;
    std::vector<Slang::ComPtr<gfx::IFramebuffer>> g_framebuffers;
    std::vector<Slang::ComPtr<gfx::ITransientResourceHeap>> g_transient_heaps;
    Slang::ComPtr<gfx::IRenderPassLayout> g_render_pass;
    Slang::ComPtr<gfx::ICommandQueue> g_queue;

    Slang::Result initialize_base(
        const char *title,
        int width,
        int height,
        gfx::DeviceType device_type = gfx::DeviceType::Default);

    void create_framebuffers(
        uint32_t width,
        uint32_t height,
        gfx::Format color_format,
        uint32_t frame_buffer_count);
    void create_swapchain_framebuffers();
    void create_offline_framebuffers();

    void main_loop();

    Slang::ComPtr<gfx::IResourceView> create_texture_from_file(
        std::string file_name,
        int &texture_width,
        int &texture_height);
    virtual void window_size_changed();

protected:
    virtual void render_frame(int framebuffer_index) = 0;

public:
    loia::platform::Window *get_window() { return g_window.get(); }
    virtual void finalize() { g_queue->waitOnHost(); }
    void offline_render();
};

struct ExampleResources {
    std::string base_dir;

    ExampleResources(const std::string &dir)
        : base_dir(dir) {
    }

    std::string resolve_resource(const char *filename) const {
        // print current working directory
        std::filesystem::path current_path = std::filesystem::current_path();
        fmt::print("Current working directory: {}\n", current_path.string());

        const std::filesystem::path path = current_path / base_dir / filename;

        fmt::print("Looking for resource: {}\n", path.string());

        if (std::filesystem::exists(path))
            return path.string();

        return filename;
    }
};

loia::types::i64 get_current_time();
loia::types::i64 get_timer_frequency();

template <typename... TArgs>
inline void report_error(const char *format, TArgs... args) {
    printf(format, std::forward<TArgs>(args)...);
#ifdef _WIN32
    char buffer[4096];
    sprintf_s(buffer, format, std::forward<TArgs>(args)...);
    _Win32OutputDebugString(buffer);
#endif
}

template <typename... TArgs>
inline void log(const char *format, TArgs... args) {
    report_error(format, args...);
}

// Many Slang API functions return detailed diagnostic information
// (error messages, warnings, etc.) as a "blob" of data, or return
// a null blob pointer instead if there were no issues.
//
// For convenience, we define a subroutine that will dump the information
// in a diagnostic blob if one is produced, and skip it otherwise.
//
inline void diagnose_if_needed(slang::IBlob *diagnostics_blob) {
    if (diagnostics_blob != nullptr) {
        report_error("%s", (const char *) diagnostics_blob->getBufferPointer());
    }
}

void init_debug_callback();

template <typename TApp>
int inner_main(int argc, char **argv) {
    init_debug_callback();

    TApp app;

    app.parse_option(argc, argv);
    if (SLANG_FAILED(app.initialize())) {
        return -1;
    }

    if (!app.is_test_mode()) {
        loia::platform::Application::run(app.get_window());
    } else {
        app.offline_render();
    }

    app.finalize();
    return 0;
}