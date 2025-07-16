#pragma once

#include <cstdint>
#include <functional>

#include "util/types.h"
#include "util/smart-pointer.h"

namespace loia::platform {

enum struct KeyCode : u32 {
    NONE = 0,
    LEFT = 0x25,
    UP = 0x26,
    DOWN = 0x28,
    RIGHT = 0x27,
    ESCAPE = 0x1B,
    RETURN = 0x0D,
    SPACE = 0x20,
    SHIFT = 0x10,
    CTRL = 0x11,
    ALT = 0x12,
    BACKSPACE = 0x08,
    DELETE = 0x2E,
    HOME = 0x24,
    END = 0x23,
    PAGE_UP = 0x21,
    PAGE_DOWN = 0x22,
    INSERT = 0x2D,
    TAB = 0x09,
    A = 0x41,
    B = 0x42,
    C = 0x43,
    D = 0x44,
    E = 0x45,
    F = 0x46,
    G = 0x47,
    H = 0x48,
    I = 0x49,
    J = 0x4A,
    K = 0x4B,
    L = 0x4C,
    M = 0x4D,
    N = 0x4E,
    O = 0x4F,
    P = 0x50,
    Q = 0x51,
    R = 0x52,
    S = 0x53,
    T = 0x54,
    U = 0x55,
    V = 0x56,
    W = 0x57,
    X = 0x58,
    Y = 0x59,
    Z = 0x5A,
    SEMICOLON = 0xBA,
    COMMA = 0xBC,
    DOT = 0xBE,
    SLASH = 0xBF,
    QUOTE = 0xDE,
    LBRACKET = 0xDB,
    RBRACKET = 0xDD,
    BACKSLASH = 0xDC,
    MINUS = 0xBD,
    PLUS = 0xBB,
    TILDE = 0xC0,
    KEY0 = 0x30,
    KEY1 = 0x31,
    KEY2 = 0x32,
    KEY3 = 0x33,
    KEY4 = 0x34,
    KEY5 = 0x35,
    KEY6 = 0x36,
    KEY7 = 0x37,
    KEY8 = 0x38,
    KEY9 = 0x39,
    F1 = 0x70,
    F2 = 0x71,
    F3 = 0x72,
    F4 = 0x73,
    F5 = 0x74,
    F6 = 0x75,
    F7 = 0x76,
    F8 = 0x77,
    F9 = 0x78,
    F10 = 0x79,
    F11 = 0x7A,
    F12 = 0x7B,
};

struct WindowHandle {
    enum class Type {
        UNKNOWN,
        WIN32_HANDLE,
        NS_WINDOW_HANDLE,
        XLIB_HANDLE,
    };
    Type type;
    loia::iptr handle_values[2];
    static WindowHandle from_hwnd(void *hwnd) {
        WindowHandle handle = {};
        handle.type = WindowHandle::Type::WIN32_HANDLE;
        handle.handle_values[0] = (loia::iptr) (hwnd);
        return handle;
    }
    static WindowHandle from_ns_window(void *nswindow) {
        WindowHandle handle = {};
        handle.type = WindowHandle::Type::NS_WINDOW_HANDLE;
        handle.handle_values[0] = (loia::iptr) (nswindow);
        return handle;
    }
    static WindowHandle from_x_window(void *xdisplay, uint32_t xwindow) {
        WindowHandle handle = {};
        handle.type = WindowHandle::Type::XLIB_HANDLE;
        handle.handle_values[0] = (loia::iptr) (xdisplay);
        handle.handle_values[1] = xwindow;
        return handle;
    }
    template <typename T>
    T convert() {
        T result;
        result.type = (decltype(result.type)) type;
        result.handleValues[0] = handle_values[0];
        result.handleValues[1] = handle_values[1];
        return result;
    }
};

struct ButtonState {
    enum Enum {
        NONE = 0,
        LEFT_BUTTON = 1,
        RIGHT_BUTTON = 2,
        MIDDLE_BUTTON = 4,
        SHIFT = 8,
        CONTROL = 16,
        ALT = 32
    };
};

struct KeyEventArgs {
    KeyCode key;
    wchar_t key_char; // For KeyPress event
    ButtonState::Enum buttons;
    bool cancel_event;
};

struct MouseEventArgs {
    int x, y;
    int delta;
    ButtonState::Enum buttons;
};

struct Rect {
    int x, y;
    int width, height;
};

enum class WindowStyle {
    DEFAULT,
    FIXED_SIZE,
};

struct WindowDesc {
    char const *title = nullptr;
    int width = 0;
    int height = 0;
    WindowStyle style = WindowStyle::DEFAULT;
};

struct Window : RefObject {
    struct Events final {
        std::function<void()> main_loop;
        std::function<void()> size_changed;
        std::function<void()> focus;
        std::function<void()> lost_focus;
        std::function<void(KeyEventArgs &)> key_down;
        std::function<void(KeyEventArgs &)> key_up;
        std::function<void(KeyEventArgs &)> key_press;
        std::function<void(MouseEventArgs)> mouse_move;
        std::function<void(MouseEventArgs)> mouse_wheel;
        std::function<void(MouseEventArgs)> mouse_down;
        std::function<void(MouseEventArgs)> mouse_up;
    };

    Events events;

    virtual void set_client_size(u32 width, u32 height) = 0;
    virtual Rect get_client_rect() = 0;
    virtual void center_screen() = 0;
    virtual void close() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool is_visible() = 0;
    virtual bool is_focused() = 0;
    virtual WindowHandle get_handle() = 0;
    virtual void set_title(const char *title) = 0;
    virtual int get_current_dpi() = 0;
};

struct Application {
    static Window *create_window(WindowDesc const &desc);
    static void init();
    static void run(Window *main_window, bool wait_for_events = false);
    static void quit();
    static void do_events();
    static void dispose();
};

} // namespace loia::platform

#ifdef _WIN32

#ifdef _MSC_VER
#ifdef _DEBUG
#define GFX_DUMP_LEAK _CrtDumpMemoryLeaks();
#endif
#endif

#endif

#ifndef GFX_DUMP_LEAK
#define GFX_DUMP_LEAK
#endif

#define PLATFORM_UI_MAIN(APPLICATION_ENTRY)      \
    int exampleMain(int argc, char **argv) {     \
        loia::platform::Application::init();     \
        auto rs = APPLICATION_ENTRY(argc, argv); \
        loia::platform::Application::dispose();  \
        GFX_DUMP_LEAK                            \
        return rs;                               \
    }