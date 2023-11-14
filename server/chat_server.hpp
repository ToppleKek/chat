/*
    Chat server platform layer module. Allows the application to implement the basic
    runtime functions init and deinit, and allows the platform layer to
    implement platform specific services (eg. file I/O).
    This is a slimmed down version of the client platform layer.
    See the documentation for the client platform layer.

    Author: Braeden Hong
      Date: October 30, 2023
*/

#pragma once
#include "../util.hpp"
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error "Unsupported platform"
#endif

namespace ChatServer {
void init();
void deinit();

std::FILE *platform_open_file(const std::string &path, const std::string &mode);
bool platform_file_exists(const char *path);
Util::IchigoVector<std::string> platform_recurse_directory(const std::string &path, const char **extension_filter, const u16 extension_filter_count);
}
