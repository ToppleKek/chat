/*
Server connection module implementation.

Author: Braeden Hong
  Date: November 5, 2023 - November 12 2023
*/

#include "server_connection.hpp"
#include "chat_client.hpp"
#include <thread>
#include <mutex>

Util::IchigoVector<ClientUser> ServerConnection::cached_users;
Util::IchigoVector<Group> ServerConnection::cached_groups;
Util::IchigoVector<ClientMessage> ServerConnection::cached_inbox;
Util::IchigoVector<ClientMessage> ServerConnection::cached_outbox;
ClientUser ServerConnection::logged_in_user("");

// The socket file descriptor of the open connection
static u32 socket_fd = INVALID_SOCKET;
// Static receiving buffer.
static char buffer[4096]{};
// Heartbeat thread. Keeps the connection alive even if the UI is blocking.
static std::thread heartbeat_thread;
// This mutex is locked before the heartbeat thread enters. Unlock to wake heartbeat thread so it can kill itself.
static std::timed_mutex kill_heartbeat_mutex;
// Guard socket access between main thread and heartbeat thread.
static std::mutex socket_access_mutex;

/*
    Find the index of a user by name.
    Parameter 'name': The name of the user to find.
    Returns the index of the user or -1 if not found.
*/
static i32 find_user_index_by_name(const std::string &name) {
    for (u32 i = 0; i < ServerConnection::cached_users.size(); ++i) {
        if (ServerConnection::cached_users.at(i).name() == name)
            return i;
    }

    return -1;
}

/*
    The heartbeat thread entry procedure.

    The fow between the client and server is as follows:
    1. Send HEARTBEAT opcode.
    2. Receive a result. Assert that it is Error::SUCCESS.
*/
static void thread_proc() {
    for (;;) {
        // Sleep for at most 10 seconds. If the mutex is ever actually unlocked, it is a signal that the application is shutting down.
        // This means that the thread should exit.
        if (kill_heartbeat_mutex.try_lock_for(std::chrono::seconds(10)))
            break;

        socket_access_mutex.lock();

        buffer[0] = Opcode::HEARTBEAT;
        send(socket_fd, buffer, 1, 0);
        i8 result;
        recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

        assert(result == Error::SUCCESS);

        socket_access_mutex.unlock();
    }
}

void ServerConnection::connect_to_server() {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    sockaddr_in server_addr{};
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    InetPton(AF_INET, "127.0.0.1", &server_addr.sin_addr.S_un.S_addr);

    if (connect(socket_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::printf("connect: SOCKET_ERROR\n");
        std::exit(1);
    }

    kill_heartbeat_mutex.lock();
    heartbeat_thread = std::thread{thread_proc};
}

bool ServerConnection::send_message(ClientMessage &message) {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    return message.send(socket_fd, ServerConnection::logged_in_user.id());
}

bool ServerConnection::delete_message(ClientMessage &message) {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    bool ret = message.delete_from_server(socket_fd, ServerConnection::logged_in_user.id());
    ServerConnection::cached_inbox.remove(ServerConnection::cached_inbox.index_of(message));
    return ret;
}

bool ServerConnection::set_status_of_logged_in_user(const std::string &status) {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    return ServerConnection::logged_in_user.set_status(socket_fd, status);
}

bool ServerConnection::register_user(const std::string &username) {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    buffer[0] = Opcode::REGISTER;
    send(socket_fd, buffer, 1, 0);
    send(socket_fd, username.c_str(), username.length(), 0);

    i8 result;
    recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

    return result == Error::SUCCESS;
}

bool ServerConnection::register_group(const std::string &name, const Util::IchigoVector<std::string> &usernames) {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    buffer[0] = Opcode::REGISTER_GROUP;
    send(socket_fd, buffer, 1, 0);
    u32 length = name.length();
    send(socket_fd, reinterpret_cast<char *>(&length), sizeof(length), 0);
    send(socket_fd, name.c_str(), name.length(), 0);

    i8 result;
    recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

    if (result != Error::SUCCESS)
        return false;

    u32 count = usernames.size();
    send(socket_fd, reinterpret_cast<char *>(&count), sizeof(count), 0);

    for (u32 i = 0; i < count; ++i) {
        length = usernames.at(i).length();
        send(socket_fd, reinterpret_cast<char *>(&length), sizeof(length), 0);
        send(socket_fd, usernames.at(i).c_str(), usernames.at(i).length(), 0);
    }

    recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);
    return result == Error::SUCCESS;
}

bool ServerConnection::login(const std::string &username) {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    std::printf("Attempting login with username=%s\n", username.c_str());

    buffer[0] = Opcode::LOGIN;
    send(socket_fd, buffer, 1, 0);
    send(socket_fd, username.c_str(), username.length(), 0);

    i32 id;
    i8 result;
    recv(socket_fd, reinterpret_cast<char *>(&id), 4, 0);
    recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

    if (result == Error::SUCCESS) {
        ServerConnection::logged_in_user = ClientUser(username);
        ServerConnection::logged_in_user.set_logged_in(true);
        ServerConnection::logged_in_user.User::set_status("Online");
        ServerConnection::logged_in_user.set_id(id);
        return true;
    }

    return false;
}

bool ServerConnection::logout() {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    std::printf("Attempting to logout\n");

    buffer[0] = Opcode::LOGOUT;
    send(socket_fd, buffer, 1, 0);

    i32 id = ServerConnection::logged_in_user.id();
    send(socket_fd, reinterpret_cast<char *>(&id), 4, 0);

    i8 result;
    recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

    if (result == Error::SUCCESS) {
        ServerConnection::logged_in_user = ClientUser("");
        ServerConnection::cached_users.clear();
        ServerConnection::cached_inbox.clear();
        return true;
    }

    return false;
}

i32 ServerConnection::refresh() {
    std::lock_guard<std::mutex> guard(socket_access_mutex);

    if (!ServerConnection::logged_in_user.is_logged_in())
        return 0;

    {
        buffer[0] = Opcode::GET_USERS;
        send(socket_fd, buffer, 1, 0);
        std::printf("Sent GET_USERS request.\n");

        std::printf("%s\n", ServerConnection::logged_in_user.name().c_str());
        i32 id = ServerConnection::logged_in_user.id();
        send(socket_fd, reinterpret_cast<char *>(&id), 4, 0);

        i8 result;
        recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

        // TODO: Report this failure?
        if (result != Error::SUCCESS) {
            return 0;
        }

        u32 user_count;
        recv(socket_fd, reinterpret_cast<char *>(&user_count), 4, 0);
        std::printf("Number of users: %u\n", user_count);
        cached_users.clear();

        for (u32 i = 0; i < user_count; ++i) {
            u32 length;
            recv(socket_fd, reinterpret_cast<char *>(&length), 4, 0);
            recv(socket_fd, buffer, length, 0);
            buffer[length] = 0;
            printf("User: %s ", buffer);
            ClientUser user(buffer);

            recv(socket_fd, reinterpret_cast<char *>(&length), 4, 0);
            recv(socket_fd, buffer, length, 0);
            buffer[length] = 0;
            printf("Status: %s\n", buffer);
            user.User::set_status(buffer);

            cached_users.append(user);
        }

        std::printf("Received all users.\n");

        // TODO: Report this status?
        recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);
    }

    {
        buffer[0] = Opcode::GET_GROUPS;
        send(socket_fd, buffer, 1, 0);

        i32 id = ServerConnection::logged_in_user.id();
        send(socket_fd, reinterpret_cast<char *>(&id), 4, 0);

        i8 result;
        recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);

        // TODO: Report this failure?
        if (result != Error::SUCCESS) {
            return 0;
        }

        u32 group_count;
        recv(socket_fd, reinterpret_cast<char *>(&group_count), sizeof(group_count), 0);
        std::printf("Number of groups: %u\n", group_count);
        cached_groups.clear();

        for (u32 i = 0; i < group_count; ++i) {
            u32 length;
            recv(socket_fd, reinterpret_cast<char *>(&length), sizeof(length), 0);
            recv(socket_fd, buffer, length, 0);
            buffer[length] = 0;

            std::string group_name = buffer;
            u32 group_user_count;
            recv(socket_fd, reinterpret_cast<char *>(&group_user_count), sizeof(group_user_count), 0);
            Util::IchigoVector<std::string> usernames(group_user_count);

            for (u32 j = 0; j < group_user_count; ++j) {
                recv(socket_fd, reinterpret_cast<char *>(&length), sizeof(length), 0);
                recv(socket_fd, buffer, length, 0);
                buffer[length] = 0;
                usernames.append(buffer);
            }

            cached_groups.append(Group(group_name, std::move(usernames)));
        }

        // TODO: Report this status?
        recv(socket_fd, reinterpret_cast<char *>(&result), 1, 0);
    }

    {
        buffer[0] = Opcode::GET_MESSAGES;
        send(socket_fd, buffer, 1, 0);
        i32 id = ServerConnection::logged_in_user.id();
        send(socket_fd, reinterpret_cast<char *>(&id), sizeof(id), 0);

        i8 result;
        recv(socket_fd, reinterpret_cast<char *>(&result), sizeof(result), 0);

        if (result != Error::SUCCESS) {
            return 0;
        }

        u32 message_count;
        recv(socket_fd, reinterpret_cast<char *>(&message_count), sizeof(message_count), 0);
        std::printf("Number of messages: %u\n", message_count);

        u32 old_message_count = cached_inbox.size();
        for (u32 i = 0; i < message_count; ++i) {
            i32 message_id = -1;
            recv(socket_fd, reinterpret_cast<char *>(&message_id), sizeof(message_id), 0);

            u32 size;
            recv(socket_fd, reinterpret_cast<char *>(&size), sizeof(size), 0);
            i32 n = recv(socket_fd, buffer, size, 0);
            assert(n != -1);
            buffer[n] = 0;

            i32 index = find_user_index_by_name(buffer);
            assert(index != -1);

            recv(socket_fd, reinterpret_cast<char *>(&size), sizeof(size), 0);
            n = recv(socket_fd, buffer, size, 0);
            assert(n != -1);
            buffer[n] = 0;


            for (u32 i = 0; i < cached_inbox.size(); ++i) {
                if (cached_inbox.at(i).id() == message_id)
                    goto next;
            }

            cached_inbox.append(ClientMessage(buffer, &ServerConnection::logged_in_user, &cached_users.at(index), message_id));
next:;
        }

        recv(socket_fd, reinterpret_cast<char *>(&result), sizeof(result), 0);
        assert(result == Error::SUCCESS);

        return cached_inbox.size() - old_message_count;
    }
}

void ServerConnection::deinit() {
    if (ServerConnection::logged_in_user.is_logged_in())
        ServerConnection::logout();

    kill_heartbeat_mutex.unlock();
    heartbeat_thread.join();

    buffer[0] = Opcode::GOODBYE;
    send(socket_fd, buffer, 1, 0);
    closesocket(socket_fd);
}
