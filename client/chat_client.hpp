#pragma once
#include "../util.hpp"
#include "vulkan.hpp"
#include <string>

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

std::FILE *platform_open_file(const std::string &path, const std::string &mode);
bool platform_file_exists(const char *path);
Util::IchigoVector<std::string> platform_recurse_directory(const std::string &path, const char **extension_filter, const u16 extension_filter_count);
}
