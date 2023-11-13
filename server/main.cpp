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
#include "../group.hpp"
#include "journal.hpp"

#define RETURN_IF_DROPPED(RECV_RET)                \
{                                                  \
    if (RECV_RET == -1) {                          \
        ICHIGO_ERROR("Client dropped connection"); \
        return;                                    \
    }                                              \
}                                                  \

static char buffer[4096]{};
static i32 next_id = 0;
static Util::IchigoVector<pollfd> poll_connection_fds;
static Util::IchigoVector<ServerUser> users;
static Util::IchigoVector<Group> groups;
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

static i32 find_group_index_by_name(const std::string &name) {
    for (u32 i = 0; i < groups.size(); ++i) {
        if (groups.at(i).name() == name)
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

static i32 find_message_index_by_id(i32 id) {
    for (u32 i = 0; i < messages.size(); ++i) {
        if (messages.at(i).id() == id)
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

static i32 get_next_id() {
    i32 ret = ++next_id;
    Journal::UpdateIdTransaction transaction(ret);
    Journal::commit_transaction(&transaction);
    return ret;
}

static void get_users(u32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

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
    send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
    for (u64 i = 0; i < users.size(); ++i) {
        User &user = users.at(i);

        size = user.name().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, user.name().c_str(), user.name().length(), 0);

        size = user.status().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, user.status().c_str(), user.status().length(), 0);
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void get_groups(u32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

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

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    u32 size = groups.size();
    send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
    for (u64 i = 0; i < users.size(); ++i) {
        const Group &group = groups.at(i);

        // Send group name
        size = group.name().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, group.name().c_str(), group.name().length(), 0);

        // Send number of users in group
        size = group.usernames().size();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);

        // Send list of all users
        for (u32 j = 0; j < group.usernames().size(); ++j) {
            const std::string &username = group.usernames().at(j);
            size = username.length();
            send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
            send(socket, username.c_str(), username.length(), 0);
        }
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void register_user(u32 socket) {
    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    if (find_user_index_by_name(buffer) != -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    const Journal::NewUserTransaction transaction(buffer);
    Journal::commit_transaction(&transaction);

    users.append(ServerUser(buffer));
    ICHIGO_INFO("Registered user: %s", buffer);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void register_group(u32 socket) {
    u32 length;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&length), sizeof(length)));
    i32 n = poll_recv(socket, buffer, length);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    if (find_group_index_by_name(buffer) != -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    std::string group_name = buffer;
    ICHIGO_INFO("New group: %s", group_name.c_str());

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    Util::IchigoVector<std::string> group_users;
    u32 user_count;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&user_count), sizeof(user_count)));
    ICHIGO_INFO("User count: %u", user_count);

    bool failed = false;
    for (u32 i = 0; i < user_count; ++i) {
        RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&length), sizeof(length)));
        RETURN_IF_DROPPED(poll_recv(socket, buffer, length));
        buffer[length] = 0;

        ICHIGO_INFO("User: %s", buffer);

        i32 user_index = find_user_index_by_name(buffer);

        if (user_index == -1)
            failed = true;
        else
            group_users.append(buffer);
    }

    if (!failed) {
        const Journal::NewGroupTransaction transaction(group_name, group_users);
        Journal::commit_transaction(&transaction);
    }

    groups.append(Group(group_name, std::move(group_users)));
    buffer[0] = failed ? Error::INVALID_REQUEST : Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void login(u32 socket) {
    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    i32 index = find_user_index_by_name(buffer);
    if (index == -1 || users.at(index).is_logged_in()) {
        ICHIGO_INFO("User %s already logged in or does not exist.", buffer);

        i32 invalid_id = -1;
        send(socket, reinterpret_cast<char *>(&invalid_id), sizeof(invalid_id), 0);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    i32 id = get_next_id();

    users.at(index).set_status("Online");
    users.at(index).set_logged_in(true);
    users.at(index).set_last_heartbeat_time(time(nullptr));
    users.at(index).set_id(id);
    users.at(index).set_connection_fd(socket);

    send(socket, reinterpret_cast<char *>(&id), sizeof(id), 0);
    ICHIGO_INFO("User logged in: %s", buffer);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void logout(u32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in() || users.at(index).connection_fd() != socket) {
        ICHIGO_INFO("User id=%d is not logged in.", id);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    users.at(index).set_status("Offline");
    users.at(index).set_logged_in(false);
    users.at(index).set_last_heartbeat_time(0);
    users.at(index).set_id(-1);

    ICHIGO_INFO("User logged out: %s", users.at(index).name().c_str());
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

static void goodbye(u32 socket) {
    ICHIGO_INFO("Farewell socket %u", socket);

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
    i32 id;

    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in()) {
        ICHIGO_INFO("User id=%d is not logged in or was not found to update status.", id);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    if (n == 0 || n > CHAT_MAX_STATUS_LENGTH) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    users.at(index).set_status(buffer);
    ICHIGO_INFO("User \"%s\" updated status to \"%s\"", users.at(index).name().c_str(), buffer);

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

    for (u32 i = 0; i < recipient_count; ++i) {
        i32 n;
        u32 recipient_name_size;
        RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_name_size), sizeof(recipient_name_size)));
        RETURN_IF_DROPPED((n = poll_recv(socket, buffer, recipient_name_size)));
        buffer[n] = 0;
        recipients.append(&users.at(find_user_index_by_name(buffer)));
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

    i32 message_id = get_next_id();
    if (recipient_count == 1) {
        const Message message(buffer, recipients.at(0), sender, message_id);
        const Journal::NewMessageTransaction transaction(message.sender()->name(), recipients.at(0)->usernames().at(0), RECIPIENT_TYPE_USER, message.content());
        Journal::commit_transaction(&transaction);
        messages.append(message);
    } else
        assert(false); // TODO: Group?

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
    send(socket, reinterpret_cast<char *>(&message_id), sizeof(message_id), 0);
}

static void delete_message(i32 socket) {
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    i32 user_index = find_user_index_by_id(id);
    if (user_index == -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));
    i32 message_index = find_message_index_by_id(id);

    if (message_index == -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    if (messages.at(message_index).recipient()->usernames().at(0) == users.at(user_index).name()) {
        const Journal::DeleteMessageTransaction transaction(id);
        Journal::commit_transaction(&transaction);

        messages.remove(message_index);
        buffer[0] = Error::SUCCESS;
        send(socket, buffer, 1, 0);
    } else {
        buffer[0] = Error::UNAUTHORIZED;
        send(socket, buffer, 1, 0);
    }
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
            ICHIGO_INFO("Socket did not say goodbye properly, but they are assumed to be dead since the last heartbeat was a long time ago!");
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
    Journal::init("default.chatjournal");

    while (Journal::has_more_transactions()) {
        Journal::Transaction *transaction = Journal::next_transaction();
        if (!transaction) {
            ICHIGO_ERROR("Failed to parse transaction. The server will now operate without a journal!");
            break;
        }

        switch (transaction->operation()) {
            case Journal::Operation::NEW_USER: {
                Journal::NewUserTransaction *new_user_transaction = static_cast<Journal::NewUserTransaction *>(transaction);
                ICHIGO_INFO("New user read from journal: %s", new_user_transaction->username().c_str());
                users.append(ServerUser(new_user_transaction->username()));
            } break;
            case Journal::Operation::NEW_MESSAGE: {
                Journal::NewMessageTransaction *new_message_transaction = static_cast<Journal::NewMessageTransaction *>(transaction);
                ICHIGO_INFO("New message read from journal: sender=%s recipient=%s content=%s", new_message_transaction->sender().c_str(), new_message_transaction->recipient().c_str(), new_message_transaction->content().c_str());
                i32 sender_index = find_user_index_by_name(new_message_transaction->sender());
                assert(sender_index != -1);

                Recipient *recipient = nullptr;
                if (new_message_transaction->recipient_type() == RECIPIENT_TYPE_USER) {
                    i32 recipient_index = find_user_index_by_name(new_message_transaction->recipient());
                    assert(recipient_index != -1);
                    recipient = &users.at(recipient_index);
                } else if (new_message_transaction->recipient_type() == RECIPIENT_TYPE_GROUP) {
                    // TODO: Groups
                    assert(false && "Unimplemented!");
                } else {
                    ICHIGO_ERROR("Invalid recipient type when reading new message from journal");
                    continue;
                }

                assert(sender_index != -1);
                // The journal is expected to have updated the 'next id' through the 'UPDATE_ID' transaction before adding a new message
                messages.append(Message(new_message_transaction->content(), recipient, &users.at(sender_index), next_id));
            } break;
            case Journal::Operation::DELETE_MESSAGE: {
                Journal::DeleteMessageTransaction *delete_message_transaction = static_cast<Journal::DeleteMessageTransaction *>(transaction);
                ICHIGO_INFO("Deleting message id: %u", delete_message_transaction->id());
                i32 message_index = find_message_index_by_id(delete_message_transaction->id());
                assert(message_index != -1);
                messages.remove(message_index);
            }; break;
            case Journal::Operation::UPDATE_ID: {
                Journal::UpdateIdTransaction *update_id_transaction = static_cast<Journal::UpdateIdTransaction *>(transaction);
                ICHIGO_INFO("Updating next id from journal: %u", update_id_transaction->id());
                next_id = update_id_transaction->id();
            } break;
            case Journal::Operation::NEW_GROUP: {
                Journal::NewGroupTransaction *new_group_transaction = static_cast<Journal::NewGroupTransaction *>(transaction);
                ICHIGO_INFO("New group read from journal: %s users: %u", new_group_transaction->name().c_str(), new_group_transaction->user_count());
                groups.append(Group(new_group_transaction->name(), new_group_transaction->users()));
            } break;
            default: {
                ICHIGO_ERROR("Unimplemented");
            }
        }

        Journal::return_transaction(transaction);
    }

    ICHIGO_INFO("Running");

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
            ICHIGO_ERROR("Poll failed. Error code: %d", WSAGetLastError());
            break;
        }

        if (poll_result > 0 && poll_listen_fd.revents & POLLRDNORM) {
            u32 connection_fd = accept(listen_fd, nullptr, nullptr);
            poll_connection_fds.append({ connection_fd, POLLRDNORM, 0 });
            connection_heartbeat_times.append(time(nullptr));

            ICHIGO_INFO("Accepted new connection");
        }

        poll_result = WSAPoll(poll_connection_fds.data(), poll_connection_fds.size(), 1);

        if (poll_result > 0) {
            for (u32 i = 0; i < poll_connection_fds.size(); ++i) {
                if (poll_connection_fds.at(i).revents & POLLRDNORM) {
                    u32 connection_fd = poll_connection_fds.at(i).fd;

                    if (poll_recv(connection_fd, buffer, 1) == -1) {
                        ICHIGO_ERROR("Client dropped connection before sending opcode");
                        continue;
                    }

                    Opcode opcode = static_cast<Opcode>(buffer[0]);
                    ICHIGO_INFO("opcode=%d", opcode);

                    switch (opcode) {
                        case Opcode::SEND_MESSAGE:   send_message(connection_fd);   break;
                        case Opcode::DELETE_MESSAGE: delete_message(connection_fd); break;
                        case Opcode::GET_MESSAGES:   get_messages(connection_fd);   break;
                        case Opcode::REGISTER:       register_user(connection_fd);  break;
                        case Opcode::REGISTER_GROUP: register_group(connection_fd); break;
                        case Opcode::LOGIN:          login(connection_fd);          break;
                        case Opcode::LOGOUT:         logout(connection_fd);         break;
                        case Opcode::GET_USERS:      get_users(connection_fd);      break;
                        case Opcode::GET_GROUPS:     get_groups(connection_fd);     break;
                        case Opcode::SET_STATUS:     set_status(connection_fd);     break;
                        case Opcode::GOODBYE:        goodbye(connection_fd);        break;
                        case Opcode::HEARTBEAT:      heartbeat(connection_fd);      break;
                    }
                }
            }
        }

        prune_dead_connections();
    }
}

void ChatServer::deinit() {
    Journal::deinit();
}
