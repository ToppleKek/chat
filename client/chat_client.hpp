/*
    Chat client platform layer module. Allows the application to implement the basic
    runtime functions init, do_frame, and deinit, and allows the platform layer to
    implement platform specific services (eg. file I/O).

    Author: Braeden Hong
      Date: October 30, 2023
*/

#pragma once
#include "../util.hpp"
#include "vulkan.hpp"
#include <string>

// Currently we only support win32 since that is all that is required for the assignment.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error "Unsupported platform"
#endif

namespace ChatClient {
extern IchigoVulkan::Context vk_context;
extern bool must_rebuild_swapchain;
extern u32 window_width;
extern u32 window_height;
void init();
void do_frame(f32 dpi_scale);
void deinit();

/*
    Open a file with the specified mode.
    Parameter 'path': A string of the path to the file to open.
    Parameter 'mode': A string of the mode to open the file in (eg. rb, wb, a, etc.)
    Returns a standard C FILE struct.
*/
std::FILE *platform_open_file(const std::string &path, const std::string &mode);

/*
    Get a filename from the user to save a file by opening a file picker.
    Parameter 'extension_filter': An array of constant strings of valid file extensions to pick from.
    Parameter 'extension_filter_count': The size of the array.
    Returns the path to the file that the user picked, or an empty string if cancelled.
*/
const std::string platform_get_save_file_name(const char **extension_filter, const u16 extension_filter_count);

/*
    Test if a file exists.
    Parameter 'path': A path to the file to test.
    Returns whether or not the file exists.
*/
bool platform_file_exists(const char *path);

/*
    Recurse a directory listing all files in the directory and all subdirectories.
    Parameter 'path': The path to the directory to recurse.
    Parameter 'extention_filter': An array of constant strings of file extensions. Only returns files that have these extensions.
    Parameter 'extension_filter_count': The size of the array.
*/
Util::IchigoVector<std::string> platform_recurse_directory(const std::string &path, const char **extension_filter, const u16 extension_filter_count);
}
