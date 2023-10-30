#include <stdint.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
// #include "util.hpp"
#include "../common.hpp"
#include "chat_server.hpp"
#include "server_user.hpp"
#include "../message.hpp"

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
// EMBED("noto.ttf", noto_font)
}

#define RETURN_IF_DROPPED(RECV_RET)                        \
{                                                          \
    if (RECV_RET == -1) {                                  \
        std::printf("[warn] Client dropped connection\n"); \
        return;                                            \
    }                                                      \
}                                                          \

static char buffer[4096]{};
static u32 next_id = 0;
static Util::IchigoVector<ServerUser> users;
static Util::IchigoVector<Message> messages;

static i32 poll_recv(u32 socket, char *buffer, u64 buffer_length, u32 timeout) {
    pollfd poll_listen_fd {
        .fd = socket,
        .events = POLLRDNORM,
        .revents = 0,
    };

    i32 poll_result = WSAPoll(&poll_listen_fd, 1, timeout);

    if (poll_result > 0 && poll_listen_fd.revents & POLLRDNORM)
        return recv(socket, buffer, buffer_length, 0);

    return -1;
}

static i32 find_user_index_by_name(const std::string &name) {
    for (u32 i = 0; i < users.size(); ++i) {
        if (users.at(i).name() == name)
            return i;
    }

    return -1;
}

static i32 find_user_index_by_id(i32 id) {
    for (u32 i = 0; i < users.size(); ++i) {
        if (users.at(i).id() == id)
            return i;
    }

    return -1;
}

static void get_users(u32 socket) {
    // Receive id as this also is our heartbeat
    i32 id;
    if (poll_recv(socket, reinterpret_cast<char *>(&id), 4, 5) == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    i32 user_index = find_user_index_by_id(id);
    if (user_index == -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    } else if (!users.at(user_index).is_logged_in()) {
        buffer[0] = Error::UNAUTHORIZED;
        send(socket, buffer, 1, 0);
        return;
    }

    users.at(user_index).set_last_heartbeat_time(time(nullptr));
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    u32 size = users.size();
    send(socket, reinterpret_cast<char *>(&size), 4, 0);
    for (u64 i = 0; i < users.size(); ++i) {
        User &user = users.at(i);

        size = user.name().length();
        send(socket, reinterpret_cast<char *>(&size), 4, 0);
        send(socket, user.name().c_str(), user.name().length(), 0);

        size = user.status().length();
        send(socket, reinterpret_cast<char *>(&size), 4, 0);
        send(socket, user.status().c_str(), user.status().length(), 0);
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void register_user(u32 socket) {
    std::printf("[debug] waiting to receive name...");

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1, 5);
    if (n == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    buffer[n] = 0;

    if (find_user_index_by_name(buffer) != -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    users.append(ServerUser(buffer));
    std::printf("[info] Registered user: %s\n", buffer);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void login(u32 socket) {
    i32 id = next_id++;
    std::printf("[debug] waiting to receive login name...");

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1, 5);
    if (n == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    buffer[n] = 0;

    i32 index = find_user_index_by_name(buffer);
    if (index == -1 || users.at(index).is_logged_in()) {
        std::printf("[info] User %s already logged in or does not exist.", buffer);

        send(socket, reinterpret_cast<char *>(&id), 4, 0);
        --next_id;
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    users.at(index).set_status("Online");
    users.at(index).set_logged_in(true);
    users.at(index).set_last_heartbeat_time(time(nullptr));
    users.at(index).set_id(id);

    send(socket, reinterpret_cast<char *>(&id), 4, 0);
    std::printf("[info] User logged in: %s\n", buffer);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void logout(u32 socket) {
    std::printf("[debug] waiting to receive logout id...");
    i32 id;
    if (poll_recv(socket, reinterpret_cast<char *>(&id), 4, 5) == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in()) {
        std::printf("[info] User id=%d is not logged in.", id);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    users.at(index).set_status("Offline");
    users.at(index).set_logged_in(false);
    users.at(index).set_last_heartbeat_time(0);
    users.at(index).set_id(-1);

    std::printf("[info] User logged out: %s\n", users.at(index).name().c_str());
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void set_status(u32 socket) {
    std::printf("[debug] waiting to receive id to change status...\n");
    i32 id;

    if (poll_recv(socket, reinterpret_cast<char *>(&id), 4, 0) == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in()) {
        std::printf("[info] User id=%d is not logged in or was not found to update status.", id);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1, 5);
    if (n == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    std::printf("DEBUG: n=%d\n", n);
    buffer[n] = 0;

    if (n == 0 || n > CHAT_MAX_STATUS_LENGTH) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    users.at(index).set_status(buffer);
    std::printf("[info] User \"%s\" updated status to \"%s\"\n", users.at(index).name().c_str(), buffer);

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void send_message(i32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), 4, 0));

    i32 index = find_user_index_by_id(id);
    if (index == -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    User *sender = &users.at(index);
    u32 recipient_count;
    Util::IchigoVector<Recipient *> recipients;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_count), sizeof(recipient_count), 5));

    std::printf("Got message being sent to %d users\n", recipient_count);
    for (u32 i = 0; i < recipient_count; ++i) {
        i32 n;
        u32 recipient_name_size;
        RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_name_size), sizeof(recipient_name_size), 5));
        RETURN_IF_DROPPED((n = poll_recv(socket, buffer, recipient_name_size, 5)));
        buffer[n] = 0;
        std::printf("Got buffer %s\n", buffer);
        recipients.append(&users.at(find_user_index_by_name(buffer)));
        std::printf("Recipient: %s\n", recipients.at(i)->usernames().at(0).c_str());
    }

    i32 n;
    u32 message_size;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&message_size), sizeof(message_size), 5));
    RETURN_IF_DROPPED((n = poll_recv(socket, buffer, message_size, 5)));
    buffer[n] = 0;

    if (n == 0 || n > CHAT_MAX_MESSAGE_LENGTH) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    if (recipient_count == 1)
        messages.append(Message(buffer, recipients.at(0), sender));
    else
        assert(false); // TODO: Group?

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    std::printf("Message count: %llu Message content: %s Message recipient: %s\n", messages.size(), messages.at(0).content().c_str(), messages.at(0).recipient()->usernames().at(0).c_str());
}

static void get_messages(i32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), 4, 0));

    i32 index = find_user_index_by_id(id);
    if (index == -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    const std::string &username = users.at(index).name();
    Util::IchigoVector<Message> messages_for_user;
    for (u32 i = 0; i < messages.size(); ++i) {
        std::printf("Iterating message with recipient: %s\n", messages.at(i).recipient()->usernames().at(0).c_str());
        if (messages.at(i).recipient()->usernames().index_of(username) != -1)
            messages_for_user.append(messages.at(i));
    }

    u32 message_count = messages_for_user.size();
    send(socket, reinterpret_cast<char *>(&message_count), sizeof(message_count), 0);

    for (u32 i = 0; i < message_count; ++i) {
        u32 size = messages_for_user.at(i).sender()->name().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, messages_for_user.at(i).sender()->name().c_str(), size, 0);

        size = messages_for_user.at(i).content().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, messages_for_user.at(i).content().c_str(), size, 0);
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void refresh_users() {
    u64 now = time(nullptr);
    for (u32 i = 0; i < users.size(); ++i) {
        if (now - users.at(i).last_heartbeat_time() > 20 && users.at(i).is_logged_in()) {
            std::printf("[info] User \"%s\" did not logout properly, but they are assumed to be dead since the last heartbeat was a long time ago!\n", users.at(i).name().c_str());
            users.at(i).set_logged_in(false);
            users.at(i).set_status("Offline");
        }
    }
}

void ChatServer::init() {
    std::printf("Running\n");
    WSADATA wsa_data;
    i32 result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        std::printf("WSA startup failed\n");
        return;
    }

    u32 listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    InetPton(AF_INET, "127.0.0.1", &server_addr.sin_addr.S_un.S_addr);
    server_addr.sin_port = htons(8080);

    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR)
        perror("bind");

    if (listen(listen_fd, 10) == SOCKET_ERROR)
        perror("listen");

    // Set non-blocking IO mode
    unsigned long imode = 1;
    ioctlsocket(listen_fd, FIONBIO, &imode);

    pollfd poll_listen_fd {
        .fd = listen_fd,
        .events = POLLRDNORM,
        .revents = 0,
    };

    for (;;) {
        i32 poll_result = WSAPoll(&poll_listen_fd, 1, 5);

        if (poll_result == SOCKET_ERROR) {
            std::printf("[error] Poll failed. Error code: %d\n", WSAGetLastError());
            break;
        }

        if (poll_result > 0 && poll_listen_fd.revents & POLLRDNORM) {
            u32 connection_fd = accept(listen_fd, nullptr, nullptr);
            std::printf("[debug] Accepted new connection\n");
            if (poll_recv(connection_fd, buffer, 1, 5) == -1) {
                std::printf("[warn] Client dropped connection before sending opcode\n");
                continue;
            }

            Opcode opcode = static_cast<Opcode>(buffer[0]);
            std::printf("opcode=%d\n", opcode);

            switch (opcode) {
                case SEND_MESSAGE: send_message(connection_fd); break;
                case GET_MESSAGES: get_messages(connection_fd); break;
                case     REGISTER: register_user(connection_fd); break;
                case        LOGIN: login(connection_fd); break;
                case       LOGOUT: logout(connection_fd); break;
                case    GET_USERS: get_users(connection_fd); break;
                case   SET_STATUS: set_status(connection_fd); break;
            }

            closesocket(connection_fd);
        }

        refresh_users();
    }
}

void ChatServer::deinit() {
}
