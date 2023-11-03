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

#define RETURN_IF_DROPPED(RECV_RET)                        \
{                                                          \
    if (RECV_RET == -1) {                                  \
        std::printf("[warn] Client dropped connection\n"); \
        return;                                            \
    }                                                      \
}                                                          \

static char buffer[4096]{};
static u32 next_id = 0;
static Util::IchigoVector<pollfd> poll_connection_fds;
static Util::IchigoVector<ServerUser> users;
static Util::IchigoVector<Message> messages;
static Util::IchigoVector<u32> connection_heartbeat_times;

static i32 poll_recv(u32 socket, char *buffer, u64 buffer_length, u32 timeout = 200) {
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

static i32 find_user_index_by_socket_fd(u32 socket) {
    for (u32 i = 0; i < users.size(); ++i) {
        if (users.at(i).connection_fd() == socket)
            return i;
    }

    return -1;
}

static void get_users(u32 socket) {
    i32 id;
    if (poll_recv(socket, reinterpret_cast<char *>(&id), 4) == -1) {
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
    std::printf("[debug] waiting to receive name...\n");

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
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
    std::printf("[debug] waiting to receive login name...\n");

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    if (n == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    buffer[n] = 0;

    i32 index = find_user_index_by_name(buffer);
    if (index == -1 || users.at(index).is_logged_in()) {
        std::printf("[info] User %s already logged in or does not exist.\n", buffer);

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
    users.at(index).set_connection_fd(socket);

    send(socket, reinterpret_cast<char *>(&id), 4, 0);
    std::printf("[info] User logged in: %s\n", buffer);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void logout(u32 socket) {
    std::printf("[debug] waiting to receive logout id...\n");
    i32 id;
    if (poll_recv(socket, reinterpret_cast<char *>(&id), 4) == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in() || users.at(index).connection_fd() != socket) {
        std::printf("[info] User id=%d is not logged in.\n", id);
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

static void goodbye(u32 socket) {
    std::printf("[info] Farewell socket %u\n", socket);

    u32 i = 0;
    for (; i < poll_connection_fds.size(); ++i) {
        if (poll_connection_fds.at(i).fd == socket)
            break;
    }

    poll_connection_fds.remove(i);
    connection_heartbeat_times.remove(i);
    closesocket(socket);
}

static void heartbeat(u32 socket) {
    u32 i = 0;
    for (; i < poll_connection_fds.size(); ++i) {
        if (poll_connection_fds.at(i).fd == socket)
            goto success;
    }

    buffer[0] = Error::INVALID_REQUEST;
    send(socket, buffer, 1, 0);
    return;

success:
    connection_heartbeat_times.at(i) = time(nullptr);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void set_status(u32 socket) {
    std::printf("[debug] waiting to receive id to change status...\n");
    i32 id;

    if (poll_recv(socket, reinterpret_cast<char *>(&id), 4) == -1) {
        std::printf("[warn] Client dropped connection\n");
        return;
    }

    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in()) {
        std::printf("[info] User id=%d is not logged in or was not found to update status.\n", id);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
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
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), 4));

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
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_count), sizeof(recipient_count)));

    std::printf("Got message being sent to %d users\n", recipient_count);
    for (u32 i = 0; i < recipient_count; ++i) {
        i32 n;
        u32 recipient_name_size;
        RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_name_size), sizeof(recipient_name_size)));
        RETURN_IF_DROPPED((n = poll_recv(socket, buffer, recipient_name_size)));
        buffer[n] = 0;
        std::printf("Got buffer %s\n", buffer);
        recipients.append(&users.at(find_user_index_by_name(buffer)));
        std::printf("Recipient: %s\n", recipients.at(i)->usernames().at(0).c_str());
    }

    i32 n;
    u32 message_size;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&message_size), sizeof(message_size)));
    RETURN_IF_DROPPED((n = poll_recv(socket, buffer, message_size)));
    buffer[n] = 0;

    if (n == 0 || n > CHAT_MAX_MESSAGE_LENGTH) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    i32 message_id = next_id++;
    if (recipient_count == 1)
        messages.append(Message(buffer, recipients.at(0), sender, message_id));
    else
        assert(false); // TODO: Group?

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
    std::printf("Sending message id=%d\n", message_id);
    send(socket, reinterpret_cast<char *>(&message_id), sizeof(message_id), 0);
}

static void get_messages(i32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), 4));

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
        if (messages.at(i).recipient()->usernames().index_of(username) != -1)
            messages_for_user.append(messages.at(i));
    }

    u32 message_count = messages_for_user.size();
    send(socket, reinterpret_cast<char *>(&message_count), sizeof(message_count), 0);

    for (u32 i = 0; i < message_count; ++i) {
        i32 message_id = messages_for_user.at(i).id();
        send(socket, reinterpret_cast<char *>(&message_id), sizeof(message_id), 0);

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

static void prune_dead_connections() {
    u64 now = time(nullptr);
    for (u32 i = 0; i < connection_heartbeat_times.size(); ++i) {
        if (now - connection_heartbeat_times.at(i) > 20) {
            std::printf("[info] Socket did not say goodbye properly, but they are assumed to be dead since the last heartbeat was a long time ago!\n");
            i32 user_index = find_user_index_by_socket_fd(poll_connection_fds.at(i).fd);

            if (user_index != -1) {
                users.at(user_index).set_logged_in(false);
                users.at(user_index).set_status("Offline");
                users.at(user_index).set_connection_fd(-1);
            }

            goodbye(poll_connection_fds.at(i).fd);
        }
    }
}

void ChatServer::init() {
    std::printf("Running\n");

    [[maybe_unused]] WSADATA wsa_data;
    assert(WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0);

    u32 listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    InetPton(AF_INET, "127.0.0.1", &server_addr.sin_addr.S_un.S_addr);
    server_addr.sin_port = htons(8080);

    assert(bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) != SOCKET_ERROR);
    assert(listen(listen_fd, 10) != SOCKET_ERROR);

    // Set non-blocking IO mode
    unsigned long imode = 1;
    ioctlsocket(listen_fd, FIONBIO, &imode);

    pollfd poll_listen_fd {
        .fd = listen_fd,
        .events = POLLRDNORM,
        .revents = 0,
    };

    for (;;) {
        i32 poll_result = WSAPoll(&poll_listen_fd, 1, 1);

        if (poll_result == SOCKET_ERROR) {
            std::printf("[error] Poll failed. Error code: %d\n", WSAGetLastError());
            break;
        }

        if (poll_result > 0 && poll_listen_fd.revents & POLLRDNORM) {
            u32 connection_fd = accept(listen_fd, nullptr, nullptr);
            poll_connection_fds.append({ connection_fd, POLLRDNORM, 0 });
            connection_heartbeat_times.append(time(nullptr));

            std::printf("[debug] Accepted new connection\n");
        }

        poll_result = WSAPoll(poll_connection_fds.data(), poll_connection_fds.size(), 1);

        if (poll_result > 0) {
            for (u32 i = 0; i < poll_connection_fds.size(); ++i) {
                if (poll_connection_fds.at(i).revents & POLLRDNORM) {
                    u32 connection_fd = poll_connection_fds.at(i).fd;

                    if (poll_recv(connection_fd, buffer, 1) == -1) {
                        std::printf("[warn] Client dropped connection before sending opcode\n");
                        continue;
                    }

                    Opcode opcode = static_cast<Opcode>(buffer[0]);
                    std::printf("[info] opcode=%d\n", opcode);

                    switch (opcode) {
                        case SEND_MESSAGE: send_message(connection_fd); break;
                        case GET_MESSAGES: get_messages(connection_fd); break;
                        case     REGISTER: register_user(connection_fd); break;
                        case        LOGIN: login(connection_fd); break;
                        case       LOGOUT: logout(connection_fd); break;
                        case    GET_USERS: get_users(connection_fd); break;
                        case   SET_STATUS: set_status(connection_fd); break;
                        case      GOODBYE: goodbye(connection_fd); break;
                        case    HEARTBEAT: heartbeat(connection_fd); break;
                    }
                }
            }
        }

        prune_dead_connections();
    }
}

void ChatServer::deinit() {
}
