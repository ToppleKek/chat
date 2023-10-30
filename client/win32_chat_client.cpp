#define _CRT_SECURE_NO_WARNINGS
#include "../common.hpp"
#include <cstdio>
#include "chat_client.hpp"
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
// #include "vulkan/vulkan_win32.h"
#include "../thirdparty/imgui/imgui_impl_win32.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

u32 ChatClient::window_width = 1920;
u32 ChatClient::window_height = 1080;

static HWND window_handle;
static bool in_sizing_loop = false;
static bool init_completed = false;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static wchar_t *to_wide_char(const char *str) {
    i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    assert(buf_size > 0);
    wchar_t *wide_buf = new wchar_t[buf_size];
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wide_buf, buf_size);
    return wide_buf;
}

static void free_wide_char_conversion(wchar_t *buf) {
    delete[] buf;
}

std::FILE *ChatClient::platform_open_file(const std::string &path, const std::string &mode) {
    i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    i32 mode_buf_size = MultiByteToWideChar(CP_UTF8, 0, mode.c_str(), -1, nullptr, 0);
    assert(buf_size > 0 && mode_buf_size > 0);
    wchar_t *wide_buf = new wchar_t[buf_size];
    wchar_t *mode_wide_buf = new wchar_t[mode_buf_size];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_buf, buf_size);
    MultiByteToWideChar(CP_UTF8, 0, mode.c_str(), -1, mode_wide_buf, mode_buf_size);
    // std::wprintf(L"platform_open_file: wide_buf=%s mode_wide_buf=%s\n", wide_buf, mode_wide_buf);
    std::FILE *ret = _wfopen(wide_buf, mode_wide_buf);

    // TODO: Should this actually exist? If we want to check if a file exists then should we have a different platform function for it?
    // assert(ret != nullptr);
    delete[] wide_buf;
    delete[] mode_wide_buf;
    return ret;
}

bool ChatClient::platform_file_exists(const char *path) {
    wchar_t *wide_path = to_wide_char(path);
    DWORD attributes = GetFileAttributesW(wide_path);
    bool ret = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    free_wide_char_conversion(wide_path);
    return ret;
}

static bool is_filtered_file(const wchar_t *filename, const char **extension_filter, const u16 extension_filter_count) {
    // Find the last period in the file name
    u64 period_index = 0;
    for (u64 current_index = 0; filename[current_index] != '\0'; ++current_index) {
        if (filename[current_index] == '.')
            period_index = current_index;
    }

    wchar_t ext_wide[16] = {};
    for (u32 i = 0; i < extension_filter_count; ++i) {
        const char *ext = extension_filter[i];
        i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, ext, -1, nullptr, 0);
        assert(buf_size <= 16);
        MultiByteToWideChar(CP_UTF8, 0, ext, -1, ext_wide, buf_size);

        if (std::wcscmp(&filename[period_index + 1], ext_wide) == 0)
            return true;
    }

    return false;
}

void visit_directory(const wchar_t *path, Util::IchigoVector<std::string> *files, const char **extension_filter, const u16 extension_filter_count) {
    HANDLE find_handle;
    WIN32_FIND_DATAW find_data;
    wchar_t path_with_filter[2048] = {};
    std::wcscat(path_with_filter, path);
    std::wcscat(path_with_filter, L"\\*");

    if ((find_handle = FindFirstFileW(path_with_filter, &find_data)) != INVALID_HANDLE_VALUE) {
        do {
            if (std::wcscmp(find_data.cFileName, L".") == 0 || std::wcscmp(find_data.cFileName, L"..") == 0)
                continue;

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                wchar_t sub_dir[2048] = {};
                _snwprintf(sub_dir, 2048, L"%s/%s", path, find_data.cFileName);
                visit_directory(sub_dir, files, extension_filter, extension_filter_count);
            } else {
                if (!is_filtered_file(find_data.cFileName, extension_filter, extension_filter_count))
                    continue;

                wchar_t full_path[2048] = {};
                _snwprintf(full_path, 2048, L"%s/%s", path, find_data.cFileName);
                i32 u8_buf_size = WideCharToMultiByte(CP_UTF8, 0, full_path, -1, nullptr, 0, nullptr, nullptr);
                char *u8_bytes = new char[u8_buf_size]();
                WideCharToMultiByte(CP_UTF8, 0, full_path, -1, u8_bytes, u8_buf_size, nullptr, nullptr);

                files->append(u8_bytes);
                delete[] u8_bytes;
            }
        } while (FindNextFileW(find_handle, &find_data) != 0);

        FindClose(find_handle);
    } else {
        auto error = GetLastError();
        std::printf("win32 plat: Failed to create find handle! error=%lu\n", error);
    }
}

Util::IchigoVector<std::string> ChatClient::platform_recurse_directory(const std::string &path, const char **extension_filter, const u16 extension_filter_count) {
    Util::IchigoVector<std::string> ret;

    i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    assert(buf_size > 0);
    wchar_t *wide_buf = new wchar_t[buf_size]();
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_buf, buf_size);

    visit_directory(wide_buf, &ret, extension_filter, extension_filter_count);

    delete[] wide_buf;
    return ret;
}

static void platform_do_frame() {
    ImGui_ImplWin32_NewFrame();
    ChatClient::do_frame(ImGui_ImplWin32_GetDpiScaleForHwnd(window_handle));
}

static LRESULT window_proc(HWND window, u32 msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_ENTERSIZEMOVE: {
        printf("WM_ENTERSIZEMOVE\n");
        in_sizing_loop = true;
    } break;

    case WM_EXITSIZEMOVE: {
        printf("WM_EXITSIZEMOVE\n");
        in_sizing_loop = false;
    } break;

    case WM_SIZE: {
        return 0;
    } break;

    case WM_DESTROY: {
        std::printf("WM_DESTROY\n");
        PostQuitMessage(0);
        return 0;
    } break;

    case WM_CLOSE: {
        std::printf("WM_CLOSE\n");
        PostQuitMessage(0);
        return 0;
    } break;

    case WM_ACTIVATEAPP: {
        return 0;
    } break;

    case WM_TIMER: {
        if (in_sizing_loop)
            platform_do_frame();

        return 0;
    } break;

    case WM_PAINT: {
        // std::printf("WM_PAINT lparam=%lld wparam=%lld\n", lparam, wparam);
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);

        if (init_completed) {
            u32 height = paint.rcPaint.bottom - paint.rcPaint.top;
            u32 width = paint.rcPaint.right - paint.rcPaint.left;

            if (height <= 0 || width <= 0)
                break;

            if (height != ChatClient::window_height || width != ChatClient::window_width) {
                ChatClient::must_rebuild_swapchain = true;
                ChatClient::window_width = width;
                ChatClient::window_height = height;
            }

            platform_do_frame();
        }
        EndPaint(window, &paint);
        return 0;
    } break;
    case WM_MOVE: {
        // std::printf("WM_MOVE\n");
    } break;
    }

    if (ImGui_ImplWin32_WndProcHandler(window, msg, wparam, lparam))
        return 1;

    return DefWindowProc(window, msg, wparam, lparam);
}

i32 main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);

    // Create win32 window
    auto instance = GetModuleHandle(nullptr);
    WNDCLASS window_class = {};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "chat_client";
    RegisterClass(&window_class);
    window_handle = CreateWindowEx(0, window_class.lpszClassName, "Chat Client", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, ChatClient::window_width, ChatClient::window_height, nullptr,
                                   nullptr, instance, nullptr);

    assert(window_handle);

    // Ask main module to init vulkan
    static const char *extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};

    ChatClient::vk_context.init(extensions, 2);

    // Create a win32 vk surface and give it to the vulkan module
    VkWin32SurfaceCreateInfoKHR surface_create_info = {};
    surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_create_info.hinstance = instance;
    surface_create_info.hwnd = window_handle;

    VkSurfaceKHR vk_surface;
    assert(vkCreateWin32SurfaceKHR(ChatClient::vk_context.vk_instance, &surface_create_info, nullptr, &vk_surface) == VK_SUCCESS);

    ChatClient::vk_context.surface = vk_surface;
    ChatClient::init();

    // Platform init
    ImGui_ImplWin32_Init(window_handle);
    init_completed = true;
    // Main loop
    for (;;) {
        MSG message;
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                goto exit;

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        platform_do_frame();
    }

exit:
    vkDeviceWaitIdle(ChatClient::vk_context.logical_device);
    ChatClient::deinit();
    return 0;
}
