#ifdef _WIN32

#include "window.h"

#include <windows.h>
#include <windowsx.h>

#include <map>
#include <string>

#pragma comment(lib, "Gdi32")

namespace loia {

// Helper function to convert UTF-8 to wide string using Windows API
std::wstring utf8_to_wide_string(const std::string &utf8_str) {
    if (utf8_str.empty()) {
        return std::wstring();
    }

    // Get the required buffer size
    int required_size = MultiByteToWideChar(
        CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);

    if (required_size <= 0) {
        return std::wstring();
    }

    // Allocate the buffer and perform the conversion
    std::vector<wchar_t> buffer(required_size);
    MultiByteToWideChar(
        CP_UTF8, 0, utf8_str.c_str(), -1, buffer.data(), required_size);

    return std::wstring(buffer.data());
}

} // namespace loia

namespace loia::platform {

static const wchar_t *k_window_class_name = L"slang-platform-window";

typedef BOOL(WINAPI *EnableNonClientDpiScalingProc)(_In_ HWND hwnd);

class Win32AppContext {
public:
    static EnableNonClientDpiScalingProc enable_non_client_dpi_scaling;
    static RefPtr<Window> main_window;
    static std::map<HWND, Window *> windows;
    static HWND main_window_handle;
    static bool is_terminated;
    static bool is_windows81_or_greater;
};

EnableNonClientDpiScalingProc Win32AppContext::enable_non_client_dpi_scaling = nullptr;
HWND Win32AppContext::main_window_handle = nullptr;
RefPtr<Window> Win32AppContext::main_window;
std::map<HWND, Window *> Win32AppContext::windows;
bool Win32AppContext::is_terminated = false;
bool Win32AppContext::is_windows81_or_greater = false;

inline ButtonState::Enum add_button_state(ButtonState::Enum val, ButtonState::Enum new_state) {
    return (ButtonState::Enum) ((int) val | (int) new_state);
}

ButtonState::Enum get_modifier_state() {
    ButtonState::Enum result = ButtonState::Enum::NONE;
    if (GetAsyncKeyState(VK_CONTROL))
        result = add_button_state(result, ButtonState::Enum::CONTROL);
    if (GetAsyncKeyState(VK_SHIFT))
        result = add_button_state(result, ButtonState::Enum::SHIFT);
    if (GetAsyncKeyState(VK_MENU))
        result = add_button_state(result, ButtonState::Enum::ALT);
    return result;
}

ButtonState::Enum get_modifier_state(WPARAM w_param) {
    ButtonState::Enum result = ButtonState::Enum::NONE;
    if (w_param & MK_CONTROL)
        result = add_button_state(result, ButtonState::Enum::CONTROL);
    if (w_param & MK_MBUTTON)
        result = add_button_state(result, ButtonState::Enum::MIDDLE_BUTTON);
    if (w_param & MK_RBUTTON)
        result = add_button_state(result, ButtonState::Enum::RIGHT_BUTTON);
    if (w_param & MK_SHIFT)
        result = add_button_state(result, ButtonState::Enum::SHIFT);
    if (GetAsyncKeyState(VK_MENU))
        result = add_button_state(result, ButtonState::Enum::ALT);
    return result;
}

LRESULT CALLBACK wnd_proc(HWND h_wnd, UINT message, WPARAM w_param, LPARAM l_param) {
    bool use_def_proc = true;
    Window *window = nullptr;
    if (Win32AppContext::windows.contains(h_wnd)) {
        window = Win32AppContext::windows[h_wnd];
    }

    switch (message) {
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            int mx = GET_X_LPARAM(l_param);
            int my = GET_Y_LPARAM(l_param);
            bool processed = false;
            if (window) {
                window->events.mouse_up(MouseEventArgs{mx, my, 0, get_modifier_state(w_param)});
            }
        } break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            int mx = GET_X_LPARAM(l_param);
            int my = GET_Y_LPARAM(l_param);
            bool processed = false;
            if (window) {
                window->events.mouse_down(MouseEventArgs{mx, my, 0, get_modifier_state(w_param)});
            }
        } break;
        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(l_param);
            int my = GET_Y_LPARAM(l_param);
            if (window) {
                window->events.mouse_move(MouseEventArgs{mx, my, 0, get_modifier_state(w_param)});
            }
        } break;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if (window) {
                window->events.mouse_wheel(MouseEventArgs{0, 0, delta, get_modifier_state(w_param)});
            }
        } break;
        case WM_CHAR: {
            if (window) {
                KeyEventArgs key_event_args =
                    {KeyCode::NONE, (wchar_t) (w_param), ButtonState::Enum::NONE, false};
                window->events.key_press(key_event_args);
                if (key_event_args.cancel_event)
                    use_def_proc = false;
            }
        } break;
        case WM_KEYDOWN: {
            if (window) {
                KeyEventArgs key_event_args = {(KeyCode) (w_param), 0, get_modifier_state(), false};
                window->events.key_down(key_event_args);
                if (key_event_args.cancel_event)
                    use_def_proc = false;
            }
        } break;
        case WM_KEYUP: {
            if (window) {
                KeyEventArgs key_event_args = {(KeyCode) (w_param), 0, get_modifier_state(), false};
                window->events.key_up(key_event_args);
                if (key_event_args.cancel_event)
                    use_def_proc = false;
            }
        } break;
        case WM_SETFOCUS: {
            if (window) {
                window->events.focus();
            }
        } break;
        case WM_KILLFOCUS: {
            if (window) {
                window->events.lost_focus();
            }
        } break;
        case WM_SIZE: {
            if (window) {
                window->events.size_changed();
            }
        } break;
        case WM_NCCREATE: {
            if (Win32AppContext::enable_non_client_dpi_scaling)
                Win32AppContext::enable_non_client_dpi_scaling(h_wnd);
            return DefWindowProc(h_wnd, message, w_param, l_param);
        } break;
        default:
            break;
    }
    if (message == WM_DESTROY && h_wnd == Win32AppContext::main_window_handle) {
        PostQuitMessage(0);
        return 0;
    }
    if (use_def_proc)
        return DefWindowProc(h_wnd, message, w_param, l_param);
    return 0;
}

void register_window_class() {
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wcex.lpfnWndProc = wnd_proc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hIcon = 0;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
    wcex.lpszMenuName = 0;
    wcex.lpszClassName = k_window_class_name;
    wcex.hIconSm = 0;

    RegisterClassExW(&wcex);
}

void unregister_window_class() {
    UnregisterClassW(k_window_class_name, GetModuleHandle(NULL));
}

HRESULT(WINAPI *get_dpi_for_monitor)
(void *hmonitor, int dpi_type, unsigned int *dpi_x, unsigned int *dpi_y);

void Application::init() {
    *(FARPROC *) &Win32AppContext::enable_non_client_dpi_scaling =
        GetProcAddress(GetModuleHandleA("User32"), "EnableNonClientDpiScaling");
    void *(WINAPI * rtl_get_version)(LPOSVERSIONINFOEXW);
    OSVERSIONINFOEXW os_info;
    *(FARPROC *) &rtl_get_version = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

    if (rtl_get_version) {
        os_info.dwOSVersionInfoSize = sizeof(os_info);
        rtl_get_version(&os_info);
        if (os_info.dwMajorVersion > 8 || (os_info.dwMajorVersion == 8 && os_info.dwMinorVersion >= 1))
            Win32AppContext::is_windows81_or_greater = true;
    }
    HRESULT(WINAPI * set_process_dpi_awareness)(int value);
    *(FARPROC *) &set_process_dpi_awareness =
        GetProcAddress(GetModuleHandleA("Shcore"), "SetProcessDpiAwareness");
    *(FARPROC *) &get_dpi_for_monitor = GetProcAddress(GetModuleHandleA("Shcore"), "GetDpiForMonitor");
    if (set_process_dpi_awareness) {
        if (Win32AppContext::is_windows81_or_greater)
            set_process_dpi_awareness(2); // PROCESS_PER_MONITOR_DPI_AWARE
        else
            set_process_dpi_awareness(1); // PROCESS_SYSTEM_DPI_AWARE
    }
    register_window_class();
}

void do_events_impl(bool wait_for_events) {
    int has_msg = 0;
    do {
        MSG msg = {};
        has_msg =
            (wait_for_events ? GetMessage(&msg, NULL, 0, 0) : PeekMessage(&msg, NULL, 0, 0, TRUE));
        if (has_msg) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.message == WM_QUIT)
            Win32AppContext::is_terminated = true;
    } while (!Win32AppContext::is_terminated && has_msg);
}

void Application::do_events() {
    do_events_impl(false);
}

void Application::quit() {
    Win32AppContext::is_terminated = true;
}

void Application::dispose() {
    Win32AppContext::main_window = nullptr;
    Win32AppContext::windows = decltype(Win32AppContext::windows)();
    unregister_window_class();
}

void Application::run(Window *main_window, bool wait_for_events) {
    if (main_window) {
        Win32AppContext::main_window = main_window;
        Win32AppContext::main_window_handle = (HWND) main_window->get_handle().handle_values[0];
        ShowWindow(Win32AppContext::main_window_handle, SW_SHOW);
        UpdateWindow(Win32AppContext::main_window_handle);
    }
    while (!Win32AppContext::is_terminated) {
        do_events_impl(wait_for_events);
        if (Win32AppContext::is_terminated)
            break;
        if (main_window) {
            main_window->events.main_loop();
        }
    }
}

class Win32PlatformWindow : public Window {
public:
    HWND handle;
    DWORD style;
    bool visible = false;
    Win32PlatformWindow(const WindowDesc &desc) {
        DWORD window_extended_style = 0;
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        if (desc.style == WindowStyle::DEFAULT) {
            style |= WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME;
        }

        HINSTANCE instance = (HINSTANCE) GetModuleHandle(0);

        RECT window_rect;
        window_rect.left = 0;
        window_rect.top = 0;
        window_rect.bottom = desc.height;
        window_rect.right = desc.width;
        AdjustWindowRect(&window_rect, style, FALSE);

        handle = CreateWindowExW(
            window_extended_style,
            (LPWSTR) k_window_class_name,
            utf8_to_wide_string(desc.title).c_str(),
            style,
            CW_USEDEFAULT,
            0, // x, y
            window_rect.right,
            window_rect.bottom,
            NULL, // parent
            NULL, // menu
            instance,
            NULL);
        if (handle)
            Win32AppContext::windows[handle] = this;
    }

    ~Win32PlatformWindow() { close(); }

    virtual void set_client_size(u32 width, u32 height) override {
        RECT current_rect;
        GetWindowRect(handle, &current_rect);

        RECT window_rect;
        window_rect.left = current_rect.left;
        window_rect.top = current_rect.top;
        window_rect.bottom = height;
        window_rect.right = width;
        AdjustWindowRect(&window_rect, style, FALSE);

        MoveWindow(
            handle,
            window_rect.left,
            window_rect.top,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            FALSE);
    }

    virtual Rect get_client_rect() override {
        RECT current_rect;
        GetClientRect(handle, &current_rect);
        Rect rect;
        rect.x = current_rect.left;
        rect.y = current_rect.top;
        rect.width = current_rect.right - current_rect.left;
        rect.height = current_rect.bottom - current_rect.top;
        return rect;
    }

    virtual void center_screen() override {
        RECT screen_rect;
        GetClientRect(GetDesktopWindow(), &screen_rect);
        RECT current_rect;
        GetWindowRect(handle, &current_rect);

        auto width = current_rect.right - current_rect.left;
        auto height = current_rect.bottom - current_rect.top;

        auto left = (screen_rect.right - width) / 2;
        auto top = (screen_rect.bottom - height) / 2;

        MoveWindow(handle, left, top, width, height, FALSE);
    }

    virtual void close() override {
        if (handle) {
            Win32AppContext::windows.erase(handle);
        }
        DestroyWindow(handle);
        handle = NULL;
    }
    virtual bool is_focused() override { return GetFocus() == handle; }
    virtual bool is_visible() override { return visible; }
    virtual WindowHandle get_handle() override { return WindowHandle::from_hwnd(handle); }
    virtual void set_title(const char *text) override {
        SetWindowText(handle, utf8_to_wide_string(text).c_str());
    }
    virtual void show() override {
        ShowWindow(handle, SW_SHOW);
        visible = true;
    }
    virtual void hide() override {
        ShowWindow(handle, SW_HIDE);
        visible = false;
    }
    virtual int get_current_dpi() override {
        int dpi = 96;
        if (Win32AppContext::is_windows81_or_greater && get_dpi_for_monitor) {
            get_dpi_for_monitor(
                MonitorFromWindow(handle, MONITOR_DEFAULTTOPRIMARY),
                0,
                (UINT *) &dpi,
                (UINT *) &dpi);
            return dpi;
        }
        dpi = GetDeviceCaps(NULL, LOGPIXELSY);
        return dpi;
    }
};

Window *Application::create_window(const WindowDesc &desc) {
    return new Win32PlatformWindow(desc);
}

} // namespace loia::platform

#endif
