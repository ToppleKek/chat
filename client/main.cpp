/*
Main client UI module.

Globals:
scale: The current DPI scale of the application
initial_style: The original Dear ImGui style that the application was initialized with.
font_config: The font config for Dear ImGui
logged_in_user: The ClientUser of the currently logged in user
cached_users: A vector of users stored after the last heartbeat to the server
cached_inbox: A vector of messages that have the logged_in_user in the recipient field stored after the last heartbeat to the server
cached_outbox: A vector of all messages sent during this client session
last_heartbeat_time: The UNIX timestamp in seconds of the last heartbeat
current_frame: The current frame that is being processed (0 to ICHIGO_MAX_FRAMES_IN_FLIGHT - 1)
ChatClient::vk_context: The vulkan context for the application. Shared with the platform layer via the ChatClient namespace
ChatClient::must_rebuild_swapchain: Boolean stating whether or not the swapchain is out of date/suboptimal. Shared with the platform layer via the ChatClient namespace

Author: Braeden Hong
  Date: October 30, 2023
*/

#include <chrono>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include "../common.hpp"
#include "chat_client.hpp"
#include "client_user.hpp"
#include "client_message.hpp"

#include "../thirdparty/imgui/imgui.h"
#include "../thirdparty/imgui/imgui_impl_vulkan.h"

#define EMBED(FNAME, VNAME)                                                               \
    __asm__(                                                                              \
        ".section .rodata    \n"                                                          \
        ".global " #VNAME "    \n.align 16\n" #VNAME ":    \n.incbin \"" FNAME            \
        "\"       \n"                                                                     \
        ".global " #VNAME "_end\n.align 1 \n" #VNAME                                      \
        "_end:\n.byte 1                   \n"                                             \
        ".global " #VNAME "_len\n.align 16\n" #VNAME "_len:\n.int " #VNAME "_end-" #VNAME \
        "\n"                                                                              \
        ".align 16           \n.text    \n");                                             \
    extern const __declspec(align(16)) unsigned char VNAME[];                             \
    extern const __declspec(align(16)) unsigned char *const VNAME##_end;                  \
    extern const unsigned int VNAME##_len;

extern "C" {
EMBED("noto.ttf", noto_font)
EMBED("build/frag.spv", fragment_shader)
EMBED("build/vert.spv", vertex_shader)
}

static float scale = 1;
static ImGuiStyle initial_style;
static ImFontConfig font_config;

static ClientUser logged_in_user("");
static Util::IchigoVector<ClientUser> cached_users;
static Util::IchigoVector<ClientMessage> cached_inbox;
static Util::IchigoVector<ClientMessage> cached_outbox;
static u32 last_heartbeat_time = 0;

static u8 current_frame = 0;
IchigoVulkan::Context ChatClient::vk_context{};
bool ChatClient::must_rebuild_swapchain = false;

struct Vec2 {
    f32 x;
    f32 y;
};

struct Vec3 {
    f32 x;
    f32 y;
    f32 z;
};
struct Vertex {
    Vec2 pos;
    Vec3 color;
};

static const VkVertexInputBindingDescription VERTEX_BINDING_DESCRIPTION = {
    0,
    sizeof(Vertex),
    VK_VERTEX_INPUT_RATE_VERTEX
};

static const VkVertexInputAttributeDescription VERTEX_ATTRIBUTE_DESCRIPTIONS[] = {
 {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, pos)},
 {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}
};

static Vertex vertices[] = {
    {{0.0f, -0.5f}, {0.2f, 0.0f, 1.0f}},
    {{0.5f, 0.5f}, {0.2f, 0.0f, 1.0f}},
    {{0.5f, -0.5f}, {0.2f, 0.0f, 1.0f}},

    {{0.0f, -0.5f}, {0.2f, 0.0f, 1.0f}},
    {{0.5f, 0.5f}, {0.2f, 0.0f, 1.0f}},
    {{0.0f, 0.5f}, {0.2f, 0.0f, 1.0f}},
    // {{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},
    // {{0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},
    // {{0.0f, 0.5f}, {1.0f, 1.0f, 1.0f}},
};

static char buffer[4096]{};

static i32 find_user_index_by_name(const std::string &name) {
    for (u32 i = 0; i < cached_users.size(); ++i) {
        if (cached_users.at(i).name() == name)
            return i;
    }

    return -1;
}

static i32 connect_to_server() {
    u32 sockfd = 0;
    sockaddr_in server_addr{};
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    InetPton(AF_INET, "127.0.0.1", &server_addr.sin_addr.S_un.S_addr);

    if (connect(sockfd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::printf("connect: SOCKET_ERROR\n");
        return -1;
    }

    return sockfd;
}

static bool register_user(const std::string &username) {
    i32 sockfd = connect_to_server();
    assert(sockfd != -1);

    buffer[0] = Opcode::REGISTER;
    send(sockfd, buffer, 1, 0);
    send(sockfd, username.c_str(), username.length(), 0);

    i8 result;
    recv(sockfd, reinterpret_cast<char *>(&result), 1, 0);

    closesocket(sockfd);

    return result == Error::SUCCESS;
}

static bool login(const std::string &username) {
    std::printf("Attempting login with username=%s\n", username.c_str());
    i32 sockfd = connect_to_server();
    assert(sockfd != -1);

    buffer[0] = Opcode::LOGIN;
    send(sockfd, buffer, 1, 0);
    send(sockfd, username.c_str(), username.length(), 0);

    i32 id;
    i8 result;
    recv(sockfd, reinterpret_cast<char *>(&id), 4, 0);
    recv(sockfd, reinterpret_cast<char *>(&result), 1, 0);
    closesocket(sockfd);

    if (result == Error::SUCCESS) {
        logged_in_user = ClientUser(username);
        logged_in_user.set_logged_in(true);
        logged_in_user.User::set_status("Online");
        logged_in_user.set_id(id);
        return true;
    }

    return false;
}

static bool logout() {
    std::printf("Attempting to logout\n");
    i32 sockfd = connect_to_server();
    assert(sockfd != -1);

    buffer[0] = Opcode::LOGOUT;
    send(sockfd, buffer, 1, 0);

    i32 id = logged_in_user.id();
    send(sockfd, reinterpret_cast<char *>(&id), 4, 0);

    i8 result;
    recv(sockfd, reinterpret_cast<char *>(&result), 1, 0);
    closesocket(sockfd);

    if (result == Error::SUCCESS) {
        logged_in_user = ClientUser("");
        return true;
    }

    return false;
}

static void refresh() {
    if (!logged_in_user.is_logged_in())
        return;

    i32 socket = connect_to_server();
    assert(socket != -1);
    {
        buffer[0] = Opcode::GET_USERS;
        send(socket, buffer, 1, 0);
        std::printf("Sent GET_USERS request.\n");

        std::printf("%s\n", logged_in_user.name().c_str());
        // Send id as heartbeat
        i32 id = logged_in_user.id();
        send(socket, reinterpret_cast<char *>(&id), 4, 0);

        i8 result;
        recv(socket, reinterpret_cast<char *>(&result), 1, 0);

        // TODO: Report this failure?
        if (result != Error::SUCCESS) {
            closesocket(socket);
            return;
        }

        u32 user_count;
        recv(socket, reinterpret_cast<char *>(&user_count), 4, 0);
        std::printf("Number of users: %u\n", user_count);
        cached_users.clear();

        for (u32 i = 0; i < user_count; ++i) {
            u32 length;
            recv(socket, reinterpret_cast<char *>(&length), 4, 0);
            recv(socket, buffer, length, 0);
            buffer[length] = 0;
            printf("User: %s ", buffer);
            ClientUser user(buffer);

            recv(socket, reinterpret_cast<char *>(&length), 4, 0);
            recv(socket, buffer, length, 0);
            buffer[length] = 0;
            printf("Status: %s\n", buffer);
            user.User::set_status(buffer);

            cached_users.append(user);
        }

        std::printf("Received all users.\n");

        // TODO: Report this status?
        recv(socket, reinterpret_cast<char *>(&result), 1, 0);
        closesocket(socket);
    }

    socket = connect_to_server();
    {
        buffer[0] = Opcode::GET_MESSAGES;
        send(socket, buffer, 1, 0);
        i32 id = logged_in_user.id();
        send(socket, reinterpret_cast<char *>(&id), sizeof(id), 0);

        i8 result;
        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);

        if (result != Error::SUCCESS) {
            closesocket(socket);
            return;
        }

        u32 message_count;
        recv(socket, reinterpret_cast<char *>(&message_count), sizeof(message_count), 0);
        std::printf("Number of messages: %u\n", message_count);

        cached_inbox.clear();
        for (u32 i = 0; i < message_count; ++i) {
            u32 size;
            recv(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
            i32 n = recv(socket, buffer, size, 0);
            assert(n != -1);
            buffer[n] = 0;
            std::printf("Sender: %s\n", buffer);

            i32 index = find_user_index_by_name(buffer);
            assert(index != -1);

            recv(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
            n = recv(socket, buffer, size, 0);
            assert(n != -1);
            buffer[n] = 0;

            std::printf("Content: %s\n", buffer);

            cached_inbox.append(ClientMessage(buffer, &logged_in_user, &cached_users.at(index)));
        }

        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);
        assert(result == Error::SUCCESS);
    }

    closesocket(socket);
}

static void frame_render() {
    ImGui::Render();
    auto imgui_draw_data = ImGui::GetDrawData();
    if (imgui_draw_data->DisplaySize.x <= 0.0f || imgui_draw_data->DisplaySize.y <= 0.0f)
        return;

    vkWaitForFences(ChatClient::vk_context.logical_device, 1, &ChatClient::vk_context.fences[current_frame], VK_TRUE, UINT64_MAX);

    u32 image_index;
    auto err = vkAcquireNextImageKHR(ChatClient::vk_context.logical_device, ChatClient::vk_context.swapchain, UINT64_MAX, ChatClient::vk_context.image_acquired_semaphores[current_frame], VK_NULL_HANDLE, &image_index);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        ChatClient::must_rebuild_swapchain = true;
        return;
    }

    VK_ASSERT_OK(err);

    vkResetFences(ChatClient::vk_context.logical_device, 1, &ChatClient::vk_context.fences[current_frame]);
    vkResetCommandBuffer(ChatClient::vk_context.command_buffers[current_frame], 0);

    VkCommandBufferBeginInfo render_begin_info{};
    render_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    render_begin_info.flags = 0;
    render_begin_info.pInheritanceInfo = nullptr;

    VK_ASSERT_OK(vkBeginCommandBuffer(ChatClient::vk_context.command_buffers[current_frame], &render_begin_info));

    // ** DRAW BEGIN **
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass        = ChatClient::vk_context.render_pass;
    render_pass_info.framebuffer       = ChatClient::vk_context.frame_buffers.at(image_index);
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = ChatClient::vk_context.extent;

    VkClearValue clear = {{{0.0f, 0.0f, 0.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear;

    vkCmdBeginRenderPass(ChatClient::vk_context.command_buffers[current_frame], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(ChatClient::vk_context.command_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, ChatClient::vk_context.graphics_pipeline);

    VkDeviceSize vertex_buffer_offsets = 0;
    vkCmdBindVertexBuffers(ChatClient::vk_context.command_buffers[current_frame], 0, 1, &ChatClient::vk_context.vertex_buffer, &vertex_buffer_offsets);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = ChatClient::vk_context.extent.width;
    viewport.height   = ChatClient::vk_context.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(ChatClient::vk_context.command_buffers[current_frame], 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = ChatClient::vk_context.extent;
    vkCmdSetScissor(ChatClient::vk_context.command_buffers[current_frame], 0, 1, &scissor);

    vkCmdDraw(ChatClient::vk_context.command_buffers[current_frame], ARRAY_LEN(vertices), 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(imgui_draw_data, ChatClient::vk_context.command_buffers[current_frame]);
    vkCmdEndRenderPass(ChatClient::vk_context.command_buffers[current_frame]);
    // ** DRAW END **

    VK_ASSERT_OK(vkEndCommandBuffer(ChatClient::vk_context.command_buffers[current_frame]));

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &ChatClient::vk_context.image_acquired_semaphores[current_frame];
    submit_info.pWaitDstStageMask = &wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &ChatClient::vk_context.command_buffers[current_frame];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &ChatClient::vk_context.render_complete_semaphores[current_frame];

    VK_ASSERT_OK(vkQueueSubmit(ChatClient::vk_context.queue, 1, &submit_info, ChatClient::vk_context.fences[current_frame]));

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &ChatClient::vk_context.render_complete_semaphores[current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &ChatClient::vk_context.swapchain;
    present_info.pImageIndices = &image_index;

    VK_ASSERT_OK(vkQueuePresentKHR(ChatClient::vk_context.queue, &present_info));
    current_frame = (current_frame + 1) % ICHIGO_MAX_FRAMES_IN_FLIGHT;
}

void ChatClient::do_frame(float dpi_scale) {
    if (ChatClient::must_rebuild_swapchain) {
        std::printf("Rebuilding swapchain\n");
        ImGui_ImplVulkan_SetMinImageCount(2);
        ChatClient::vk_context.rebuild_swapchain(ChatClient::window_width, ChatClient::window_height);
        ChatClient::must_rebuild_swapchain = false;
    }

    if (dpi_scale != scale) {
        std::printf("scaling to scale=%f\n", dpi_scale);
        auto io = ImGui::GetIO();
        {
            io.Fonts->Clear();
            io.Fonts->AddFontFromMemoryTTF((void *) noto_font, noto_font_len, static_cast<i32>(18 * dpi_scale), &font_config, io.Fonts->GetGlyphRangesJapanese());
            io.Fonts->Build();

            vkQueueWaitIdle(ChatClient::vk_context.queue);
            ImGui_ImplVulkan_DestroyFontsTexture();

            // Upload fonts to GPU
            VkCommandPool command_pool = ChatClient::vk_context.command_pool;
            VkCommandBufferAllocateInfo allocate_info{};
            allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool        = command_pool;
            allocate_info.commandBufferCount = 1;
            allocate_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            assert(vkAllocateCommandBuffers(ChatClient::vk_context.logical_device, &allocate_info, &command_buffer) == VK_SUCCESS);

            auto err = vkResetCommandPool(ChatClient::vk_context.logical_device, command_pool, 0);
            VK_ASSERT_OK(err);
            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            err = vkBeginCommandBuffer(command_buffer, &begin_info);
            VK_ASSERT_OK(err);

            ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

            VkSubmitInfo end_info{};
            end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            end_info.commandBufferCount = 1;
            end_info.pCommandBuffers = &command_buffer;
            err = vkEndCommandBuffer(command_buffer);
            VK_ASSERT_OK(err);
            err = vkQueueSubmit(ChatClient::vk_context.queue, 1, &end_info, VK_NULL_HANDLE);
            VK_ASSERT_OK(err);

            err = vkDeviceWaitIdle(ChatClient::vk_context.logical_device);
            VK_ASSERT_OK(err);
            ImGui_ImplVulkan_DestroyFontUploadObjects();

            vkFreeCommandBuffers(ChatClient::vk_context.logical_device, command_pool, 1, &command_buffer);
        }
        ImGui::HACK_SetStyle(initial_style);
        ImGui::GetStyle().ScaleAllSizes(dpi_scale);
        scale = dpi_scale;
    }

    u32 now = time(nullptr);
    if (now - last_heartbeat_time >= 10) {
        refresh();
        last_heartbeat_time = now;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("main_window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

    static char text_input_buffer[CHAT_MAX_MESSAGE_LENGTH];
    static ClientUser *message_recipient = nullptr;
    static bool modal_request_failed = false;

    if (logged_in_user.is_logged_in()) {
        // ** Message and user tables **
        ImGui::BeginChild("message_list", ImVec2(ImGui::GetContentRegionAvail().x * 0.8f, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 4));

        if (ImGui::BeginTabBar("main_tab_bar", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
            if (ImGui::BeginTabItem("Inbox") && ImGui::BeginTable("message_table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody)) {
                ImGui::TableSetupColumn("Sender");
                ImGui::TableSetupColumn("Message");
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (u32 i = 0; i < cached_inbox.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", cached_inbox.at(i).sender()->name().c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", cached_inbox.at(i).content().c_str());
                }

                ImGui::EndTable();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Outbox") && ImGui::BeginTable("message_table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody)) {
                ImGui::TableSetupColumn("Recipient(s)");
                ImGui::TableSetupColumn("Message");
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (u32 i = 0; i < cached_outbox.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    auto usernames = cached_outbox.at(i).recipient()->usernames();
                    std::stringstream ss;
                    for (u32 j = 0; j < usernames.size(); ++j)
                        ss << usernames.at(j) << (j == usernames.size() - 1 ? "" : ", ");

                    ImGui::Text("%s", ss.str().c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", cached_outbox.at(i).content().c_str());
                }

                ImGui::EndTable();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("user_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 4));

        if (ImGui::BeginTable("user_table", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("User");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (u32 i = 0; i < cached_users.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                if (ImGui::Selectable(cached_users.at(i).name().c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    std::printf("Selected\n");
                    modal_request_failed = false;
                    std::memset(text_input_buffer, 0, ARRAY_LEN(text_input_buffer));
                    message_recipient = &cached_users.at(i);
                    ImGui::OpenPopup("Send message");
                }

                ImGui::TableNextColumn();
                ImGui::Text("%s", cached_users.at(i).status().c_str());
            }


            if (ImGui::BeginPopupModal("Send message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (modal_request_failed)
                    ImGui::Text("Send failed.");

                if (message_recipient) {
                    ImGui::Text("New message to %s", message_recipient->name().c_str());
                    ImGui::InputText("Content", text_input_buffer, ARRAY_LEN(text_input_buffer));
                    ImGui::Separator();

                    if (ImGui::Button("Send", ImVec2(120, 0))) {
                        ClientMessage message(text_input_buffer, message_recipient, &logged_in_user);
                        i32 socket = connect_to_server();
                        if (!message.send(socket, logged_in_user.id())) {
                            modal_request_failed = true;
                        } else {
                            cached_outbox.append(message);
                            ImGui::CloseCurrentPopup();
                            refresh();
                        }

                        closesocket(socket);
                    }

                    ImGui::SameLine();

                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                        ImGui::CloseCurrentPopup();

                }

                ImGui::EndPopup();
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();

        // ** Bottom interaction buttons **
        if (ImGui::Button("Refresh")) {
            refresh();
            last_heartbeat_time = now;
        }

        ImGui::SameLine();

        if (ImGui::Button("Set status...")) {
            modal_request_failed = false;
            std::memset(text_input_buffer, 0, CHAT_MAX_STATUS_LENGTH + 1);
            ImGui::OpenPopup("Set status");
        }

        ImGui::SameLine();

        if (ImGui::Button("Logout")) {
            if (!logout()) {
                std::printf("[error] Something is very wrong. We failed to logout.\n");
            } else {
                cached_users.clear();
            }
        }

        ImGui::Text("Logged in as: %s", logged_in_user.name().c_str());
    } else {
        if (ImGui::Button("Login")) {
            modal_request_failed = false;
            std::memset(text_input_buffer, 0, ARRAY_LEN(text_input_buffer));
            ImGui::OpenPopup("Login");
        }

        ImGui::SameLine();

        if (ImGui::Button("Register...")) {
            modal_request_failed = false;
            std::memset(text_input_buffer, 0, ARRAY_LEN(text_input_buffer));
            ImGui::OpenPopup("Register");
        }
    }

    // ** Popup rendering **
    if (ImGui::BeginPopupModal("Register", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (modal_request_failed)
            ImGui::Text("Registration failed.");

        ImGui::InputText("Name", text_input_buffer, ARRAY_LEN(text_input_buffer));
        ImGui::Separator();

        if (ImGui::Button("Register", ImVec2(120, 0))) {
            if (!register_user(text_input_buffer))
                modal_request_failed = true;
            else
                ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Login", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (modal_request_failed)
            ImGui::Text("Login failed.");

        ImGui::InputText("Name", text_input_buffer, ARRAY_LEN(text_input_buffer));
        ImGui::Separator();

        if (ImGui::Button("Login", ImVec2(120, 0))) {
            if (!login(text_input_buffer)) {
                modal_request_failed = true;
            } else {
                ImGui::CloseCurrentPopup();
                refresh();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Set status", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (modal_request_failed)
            ImGui::Text("Failed to update status.");

        ImGui::InputText("New status", text_input_buffer, CHAT_MAX_STATUS_LENGTH + 1);
        ImGui::Separator();

        if (ImGui::Button("Update", ImVec2(120, 0))) {
            i32 socket = connect_to_server();

            if (!logged_in_user.set_status(socket, text_input_buffer)) {
                modal_request_failed = true;
            } else {
                ImGui::CloseCurrentPopup();
                refresh();
            }

            closesocket(socket);
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::EndFrame();

    if (ChatClient::window_height != 0 && ChatClient::window_width != 0)
        frame_render();
}

void ChatClient::init() {
    font_config.FontDataOwnedByAtlas = false;
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.RasterizerMultiply = 1.5f;

    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(ChatClient::vk_context.selected_gpu, ChatClient::vk_context.queue_family_index, ChatClient::vk_context.surface, &res);
    assert(res == VK_TRUE);

    const VkFormat request_surface_image_format = VK_FORMAT_B8G8R8A8_UNORM;
    const VkColorSpaceKHR request_surface_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    // ** Select a surface format **
    uint32_t surface_format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ChatClient::vk_context.selected_gpu, ChatClient::vk_context.surface, &surface_format_count, nullptr);
    VkSurfaceFormatKHR *surface_formats = reinterpret_cast<VkSurfaceFormatKHR *>(platform_alloca(surface_format_count * sizeof(VkSurfaceFormatKHR)));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ChatClient::vk_context.selected_gpu, ChatClient::vk_context.surface, &surface_format_count, surface_formats);

    for (u32 i = 0; i < surface_format_count; ++i) {
        if (surface_formats[i].format == request_surface_image_format && surface_formats[i].colorSpace == request_surface_color_space) {
            ChatClient::vk_context.surface_format = surface_formats[i];
            goto found_format;
        }
    }

    ChatClient::vk_context.surface_format = surface_formats[0];

found_format:
    // ** Select a present format **

// #define ICHIGO_PREFERRED_PRESENT_MODE VK_PRESENT_MODE_MAILBOX_KHR
#define ICHIGO_PREFERRED_PRESENT_MODE VK_PRESENT_MODE_FIFO_KHR
    u32 present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ChatClient::vk_context.selected_gpu, ChatClient::vk_context.surface, &present_mode_count, nullptr);
    assert(present_mode_count != 0);
    VkPresentModeKHR *present_modes = reinterpret_cast<VkPresentModeKHR *>(platform_alloca(present_mode_count * sizeof(VkPresentModeKHR)));
    vkGetPhysicalDeviceSurfacePresentModesKHR(ChatClient::vk_context.selected_gpu, ChatClient::vk_context.surface, &present_mode_count, present_modes);

    for (u32 i = 0; i < present_mode_count; ++i) {
        if (present_modes[i] == ICHIGO_PREFERRED_PRESENT_MODE) {
            ChatClient::vk_context.present_mode = ICHIGO_PREFERRED_PRESENT_MODE;
            goto found_present_mode;
        }
    }

    // FIFO guaranteed to be available
    ChatClient::vk_context.present_mode = VK_PRESENT_MODE_FIFO_KHR;

found_present_mode:
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ChatClient::vk_context.selected_gpu, ChatClient::vk_context.surface, &surface_capabilities);

    // ** Swapchain, images, and image views **
    ChatClient::vk_context.create_swapchain_and_images(ChatClient::window_width, ChatClient::window_height);

    // ** Pipeline **
    // Create shaders
    VkShaderModule vertex_shader_module;
    VkShaderModule fragment_shader_module;
    {
        VkShaderModuleCreateInfo shader_create_info{};
        shader_create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_create_info.codeSize = vertex_shader_len;
        shader_create_info.pCode    = reinterpret_cast<const u32 *>(vertex_shader);
        VK_ASSERT_OK(vkCreateShaderModule(ChatClient::vk_context.logical_device, &shader_create_info, VK_NULL_HANDLE, &vertex_shader_module));
    }
    {
        VkShaderModuleCreateInfo shader_create_info{};
        shader_create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_create_info.codeSize = fragment_shader_len;
        shader_create_info.pCode    = reinterpret_cast<const u32 *>(fragment_shader);
        VK_ASSERT_OK(vkCreateShaderModule(ChatClient::vk_context.logical_device, &shader_create_info, VK_NULL_HANDLE, &fragment_shader_module));
    }

    VkPipelineShaderStageCreateInfo shader_stages[] = {
    {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader_module,
            .pName  = "main"
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader_module,
            .pName  = "main"
        }
    };

    const static VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info{};
    dynamic_state_create_info.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = 2;
    dynamic_state_create_info.pDynamicStates    = dynamic_states;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount   = 1;
    vertex_input_info.pVertexBindingDescriptions      = &VERTEX_BINDING_DESCRIPTION;
    vertex_input_info.vertexAttributeDescriptionCount = 2;
    vertex_input_info.pVertexAttributeDescriptions    = VERTEX_ATTRIBUTE_DESCRIPTIONS;

    VkPipelineInputAssemblyStateCreateInfo ia_info{};
    ia_info.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<f32>(ChatClient::vk_context.extent.width);
    viewport.height   = static_cast<f32>(ChatClient::vk_context.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset        = {0, 0};
    scissor.extent.width  = ChatClient::vk_context.extent.width;
    scissor.extent.height = ChatClient::vk_context.extent.height;

    VkPipelineViewportStateCreateInfo viewport_info{};
    viewport_info.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.pViewports    = &viewport;
    viewport_info.scissorCount  = 1;
    viewport_info.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer_create_info{};
    rasterizer_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer_create_info.depthClampEnable        = VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer_create_info.lineWidth               = 1.0f;
    rasterizer_create_info.cullMode                = VK_CULL_MODE_NONE;
    rasterizer_create_info.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable   = VK_FALSE;
    multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading      = 1.0f;
    multisampling.pSampleMask           = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable      = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_attachment{};
    color_attachment.blendEnable         = VK_TRUE;
    color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    color_attachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendStateCreateInfo blend_info{};
    blend_info.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_info.logicOpEnable   = VK_FALSE;
    blend_info.attachmentCount = 1;
    blend_info.pAttachments    = &color_attachment;

    VkPipelineLayout pipeline_layout;
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 0;
    layout_info.pSetLayouts            = nullptr;
    layout_info.pushConstantRangeCount = 0;
    layout_info.pPushConstantRanges    = nullptr;

    VK_ASSERT_OK(vkCreatePipelineLayout(ChatClient::vk_context.logical_device, &layout_info, VK_NULL_HANDLE, &pipeline_layout));

    // ** Render pass **
    VkAttachmentDescription color_attachment_description{};
    color_attachment_description.format         = ChatClient::vk_context.surface_format.format;
    color_attachment_description.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_attachment_description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment_description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment_description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment_description.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_reference{};
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass_description{};
    subpass_description.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_description.colorAttachmentCount = 1;
    subpass_description.pColorAttachments    = &color_attachment_reference;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments    = &color_attachment_description;
    render_pass_info.subpassCount    = 1;
    render_pass_info.pSubpasses      = &subpass_description;

    VkSubpassDependency dependency{};
    dependency.srcSubpass            = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass            = 0;
    dependency.srcStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask         = 0;
    dependency.dstStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask         = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies   = &dependency;

    VK_ASSERT_OK(vkCreateRenderPass(ChatClient::vk_context.logical_device, &render_pass_info, VK_NULL_HANDLE, &ChatClient::vk_context.render_pass));

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount          = 2;
    pipeline_create_info.pStages             = shader_stages;
    pipeline_create_info.pVertexInputState   = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &ia_info;
    pipeline_create_info.pViewportState      = &viewport_info;
    pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    pipeline_create_info.pMultisampleState   = &multisampling;
    pipeline_create_info.pDepthStencilState  = nullptr;
    pipeline_create_info.pColorBlendState    = &blend_info;
    pipeline_create_info.pDynamicState       = &dynamic_state_create_info;
    pipeline_create_info.layout              = pipeline_layout;
    pipeline_create_info.renderPass          = ChatClient::vk_context.render_pass;
    pipeline_create_info.subpass             = 0;

    VK_ASSERT_OK(vkCreateGraphicsPipelines(ChatClient::vk_context.logical_device, VK_NULL_HANDLE, 1, &pipeline_create_info, VK_NULL_HANDLE, &ChatClient::vk_context.graphics_pipeline));

    // ** Frame buffers **
    ChatClient::vk_context.create_framebuffers();

    // ** Command pool **
    VkCommandPoolCreateInfo command_pool_create_info{};
    command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_create_info.queueFamilyIndex = ChatClient::vk_context.queue_family_index;

    VK_ASSERT_OK(vkCreateCommandPool(ChatClient::vk_context.logical_device, &command_pool_create_info, nullptr, &ChatClient::vk_context.command_pool));

    // ** Vertex buffers **
    ChatClient::vk_context.create_buffer(
        sizeof(Vertex) * ARRAY_LEN(vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &ChatClient::vk_context.vertex_buffer,
        &ChatClient::vk_context.vertex_buffer_memory
    );

    // ** Command buffers **
    VkCommandBufferAllocateInfo command_buffer_alloc_info{};
    command_buffer_alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_alloc_info.commandPool        = ChatClient::vk_context.command_pool;
    command_buffer_alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_alloc_info.commandBufferCount = ICHIGO_MAX_FRAMES_IN_FLIGHT;

    VK_ASSERT_OK(vkAllocateCommandBuffers(ChatClient::vk_context.logical_device, &command_buffer_alloc_info, ChatClient::vk_context.command_buffers));

    // ** Syncronization **
    VkSemaphoreCreateInfo semaphore_create_info{};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_create_info{};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 i = 0; i < ICHIGO_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_ASSERT_OK(vkCreateSemaphore(ChatClient::vk_context.logical_device, &semaphore_create_info, nullptr, &ChatClient::vk_context.image_acquired_semaphores[i]));
        VK_ASSERT_OK(vkCreateSemaphore(ChatClient::vk_context.logical_device, &semaphore_create_info, nullptr, &ChatClient::vk_context.render_complete_semaphores[i]));
        VK_ASSERT_OK(vkCreateFence(ChatClient::vk_context.logical_device, &fence_create_info, nullptr, &ChatClient::vk_context.fences[i]));
    }


    // ** !!!!!! FIXME: WE SHOULD FILL THE VERTEX BUFFER EACH FRAME **
    void *vertex_buffer_data;
    vkMapMemory(ChatClient::vk_context.logical_device, ChatClient::vk_context.vertex_buffer_memory, 0, VK_WHOLE_SIZE, 0, &vertex_buffer_data);
    std::memcpy(vertex_buffer_data, vertices, sizeof(Vertex) * ARRAY_LEN(vertices));
    vkUnmapMemory(ChatClient::vk_context.logical_device, ChatClient::vk_context.vertex_buffer_memory);

    // Init imgui
    {
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance       = ChatClient::vk_context.vk_instance;
        init_info.PhysicalDevice = ChatClient::vk_context.selected_gpu;
        init_info.Device         = ChatClient::vk_context.logical_device;
        init_info.QueueFamily    = ChatClient::vk_context.queue_family_index;
        init_info.Queue          = ChatClient::vk_context.queue;
        init_info.PipelineCache  = VK_NULL_HANDLE;
        init_info.DescriptorPool = ChatClient::vk_context.descriptor_pool;
        init_info.Subpass        = 0;
        init_info.MinImageCount  = 2;
        init_info.ImageCount     = ChatClient::vk_context.swapchain_image_count;
        init_info.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGui_ImplVulkan_Init(&init_info, ChatClient::vk_context.render_pass);
        initial_style = ImGui::GetStyle();
    }

    // Fonts
    {
        auto io = ImGui::GetIO();
        // io.Fonts->AddFontFromFileTTF("noto.ttf", 18, nullptr, io.Fonts->GetGlyphRangesJapanese());
        io.Fonts->AddFontFromMemoryTTF((void *) noto_font, noto_font_len, 18, &font_config, io.Fonts->GetGlyphRangesJapanese());
        // io.Fonts->AddFontFromFileTTF("noto.ttf", 18);

        // Upload fonts to GPU
        VkCommandPool command_pool = ChatClient::vk_context.command_pool;
        VkCommandBuffer command_buffer = ChatClient::vk_context.command_buffers[current_frame];

        auto err = vkResetCommandPool(ChatClient::vk_context.logical_device, command_pool, 0);
        VK_ASSERT_OK(err);
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(command_buffer, &begin_info);
        VK_ASSERT_OK(err);

        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

        VkSubmitInfo end_info{};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        err = vkEndCommandBuffer(command_buffer);
        VK_ASSERT_OK(err);
        err = vkQueueSubmit(ChatClient::vk_context.queue, 1, &end_info, VK_NULL_HANDLE);
        VK_ASSERT_OK(err);

        err = vkDeviceWaitIdle(ChatClient::vk_context.logical_device);
        VK_ASSERT_OK(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
        // io.Fonts->Build();
    }

    WSADATA wsa_data;
    i32 result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != NO_ERROR) {
        std::printf("Failed to init wsa\n");
    }
}

void ChatClient::deinit() {
    if (logged_in_user.is_logged_in())
        logout();
}
