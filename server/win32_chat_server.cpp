#define _CRT_SECURE_NO_WARNINGS
#include "../common.hpp"
#include <cstdio>
#include "chat_server.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

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

std::FILE *ChatServer::platform_open_file(const std::string &path, const std::string &mode) {
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

bool ChatServer::platform_file_exists(const char *path) {
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

Util::IchigoVector<std::string> ChatServer::platform_recurse_directory(const std::string &path, const char **extension_filter, const u16 extension_filter_count) {
    Util::IchigoVector<std::string> ret;

    i32 buf_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    assert(buf_size > 0);
    wchar_t *wide_buf = new wchar_t[buf_size]();
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_buf, buf_size);

    visit_directory(wide_buf, &ret, extension_filter, extension_filter_count);

    delete[] wide_buf;
    return ret;
}

i32 main() {
    SetConsoleOutputCP(CP_UTF8);
    ChatServer::init();
    ChatServer::deinit();
    return 0;
}
