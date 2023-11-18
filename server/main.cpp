/*
    Main server module. Handles incoming connections, user storage, group storage, message storage, and all interactions with clients.

    Globals:
    buffer: A general purpose 4kb buffer used for socket communication.
    next_id: The ID that will be used next.
    poll_connection_fds: A vector of the poll structs defining how each socket should be polled for new data.
    users: A vector of all users.
    groups: A vector of all groups.
    messages: A vector of all messages.
    connection_heartbeat_times: A vector containing the last heartbeat times of each user. Kept in sync with poll_connection_fds.

    Author: Braeden Hong
      Date: October 30, 2023 - November 12 2023
*/

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include "../common.hpp"
#include "chat_server.hpp"
#include "server_user.hpp"
#include "../message.hpp"
#include "../group.hpp"
#include "journal.hpp"

// A macro for returning from all conversation functions if a poll fails (ie. the client has dropped the connection mid conversation).
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

/*
    Poll the specified socket for new data and receive it if data is made available before the connection times out.
    Parameter 'socket': The socket to poll and receive data from.
    Parameter 'buffer': The buffer to write the response data into.
    Parameter 'buffer_length': The length of said buffer.
    Parameter 'timeout': The timeout in miliseconds (default 200).
*/
static i32 poll_recv(u32 socket, char *buffer, u64 buffer_length, u32 timeout = 200) {
    pollfd poll_listen_fd {
        .fd = socket,
        .events = POLLRDNORM,
        .revents = 0,
    };

    // Windows equivalent to posix poll().
    i32 poll_result = WSAPoll(&poll_listen_fd, 1, timeout);

    if (poll_result > 0 && poll_listen_fd.revents & POLLRDNORM)
        return recv(socket, buffer, buffer_length, 0);

    return -1;
}

/*
    Get the index of a user by their username.
    Parameter 'name': The username to search for.
    Returns the index of the user in the users vector if found, -1 if not.
*/
static i32 find_user_index_by_name(const std::string &name) {
    for (u32 i = 0; i < users.size(); ++i) {
        if (users.at(i).name() == name)
            return i;
    }

    return -1;
}

/*
    Get the index of a group by the group's name.
    Parameter 'name': The group name to search for.
    Returns the index of the group in the groups vector if found, -1 if not.
*/
static i32 find_group_index_by_name(const std::string &name) {
    for (u32 i = 0; i < groups.size(); ++i) {
        if (groups.at(i).name() == name)
            return i;
    }

    return -1;
}

/*
    Get the index of a user by their id.
    Parameter 'id': The id to search for.
    Returns the index of the user in the users vector if found, -1 if not.
*/
static i32 find_user_index_by_id(i32 id) {
    for (u32 i = 0; i < users.size(); ++i) {
        if (users.at(i).id() == id)
            return i;
    }

    return -1;
}

/*
    Get the index of a message by its id.
    Parameter 'id': The id to search for.
    Returns the index of the message in the messages vector if found, -1 if not.
*/
static i32 find_message_index_by_id(i32 id) {
    for (u32 i = 0; i < messages.size(); ++i) {
        if (messages.at(i).id() == id)
            return i;
    }

    return -1;
}

/*
    Get the index of a user by the TCP socket file descriptor of the client that is logged in as them.
    Parameter 'socket': The file descriptor to search for.
    Returns the index of the user in the users vector if found, -1 if not.
*/
static i32 find_user_index_by_socket_fd(u32 socket) {
    for (u32 i = 0; i < users.size(); ++i) {
        if (users.at(i).connection_fd() == socket)
            return i;
    }

    return -1;
}

/*
    Automatically commit an UPDATE_ID transaction to the journal and get the next ID.
    Returns the next ID to use.
*/
static i32 get_next_id() {
    i32 ret = ++next_id;
    Journal::UpdateIdTransaction transaction(ret);
    Journal::commit_transaction(&transaction);
    return ret;
}

/*
    Get all users conversation function.

    The flow between the server and the client is the same as outlined in the client documentation,
    but it must be noted that this side (server side) of the conversation has additional checks (obviously).

    1. Receive the ID of the logged in user.
    2. Verify that this user exists. Send Error::INVALID_REQUEST if not. Abort.
    3. Verify that this user is logged in. Send Error::UNAUTHORIZED if not. Abort.
    4. Send Error::SUCCESS.
    5. Send the number of users.
    6. Send n username and status pairs (2 strings).
    7. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void get_users(u32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    i32 user_index = find_user_index_by_id(id);
    if (user_index == -1) {
        // Step 2
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    } else if (!users.at(user_index).is_logged_in()) {
        // Step 3
        buffer[0] = Error::UNAUTHORIZED;
        send(socket, buffer, 1, 0);
        return;
    }

    // TODO: Unused for now. Has been replaced by the heartbeat vector.
    users.at(user_index).set_last_heartbeat_time(time(nullptr));

    // Step 4
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    // Step 5
    u32 size = users.size();
    send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
    // Step 6
    for (u64 i = 0; i < users.size(); ++i) {
        User &user = users.at(i);

        size = user.name().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, user.name().c_str(), user.name().length(), 0);

        size = user.status().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, user.status().c_str(), user.status().length(), 0);
    }

    // Step 7
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Get all groups conversation function.

    The flow between the server and the client is as follows:
    1. Receive the ID of the logged in user.
    2. Verify that this user exists. Send Error::INVALID_REQUEST if not. Abort.
    3. Verify that this user is logged in. Send Error::UNAUTHORIZED if not. Abort.
    4. Send Error::SUCCESS.
    5. Send the number of groups (n).
    6. Send n groups by doing the following:
        6a. Send the group name.
        6b. Send the number of users in the group (m).
        6c. Send m usernames. These are the users in the group.
    7. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void get_groups(u32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    i32 user_index = find_user_index_by_id(id);
    if (user_index == -1) {
        // Step 2
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    } else if (!users.at(user_index).is_logged_in()) {
        // Step 3
        buffer[0] = Error::UNAUTHORIZED;
        send(socket, buffer, 1, 0);
        return;
    }

    // Step 4
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    // Step 5
    u32 size = groups.size();
    send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
    // Step 6
    for (u64 i = 0; i < groups.size(); ++i) {
        const Group &group = groups.at(i);

        // Step 6a
        size = group.name().length();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(socket, group.name().c_str(), group.name().length(), 0);

        // Step 6b
        auto usernames = group.usernames();
        size = usernames.size();
        send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);

        // Step 6c
        for (u32 j = 0; j < usernames.size(); ++j) {
            const std::string &username = usernames.at(j);
            size = username.size();
            send(socket, reinterpret_cast<char *>(&size), sizeof(size), 0);
            send(socket, username.c_str(), username.length(), 0);
        }
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Register a new user.

    The flow between the server and the client is as follows:
    1. Receive the name of the group to register as a string.
    2. Ensure that that username is not already in use. Send Error::INVALID_REQUEST and abort if it is.
    3. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void register_user(u32 socket) {
    // Step 1
    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    // Step 2
    if (find_user_index_by_name(buffer) != -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    const Journal::NewUserTransaction transaction(buffer);
    Journal::commit_transaction(&transaction);

    users.append(ServerUser(buffer));
    ICHIGO_INFO("Registered user: %s", buffer);

    // Step 3
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Register a new group.

    The flow between the server and client is as follows:
    1. Receive the name of the group to register as a string.
    2. Check if the group name already exists. If it does, send Error::INVALID_REQUEST and abort.
    3. Send Error::SUCCESS.
    4. Receive the number of users in the group.
    5. Receive n username strings. If a user does not exist, take note of this.
    6. If any usernames were not resolved in step 5, send Error::INVALID_REQUEST. Otherwise, send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void register_group(u32 socket) {
    // Step 1
    u32 length;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&length), sizeof(length)));
    i32 n = poll_recv(socket, buffer, length);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    // Step 2
    if (find_group_index_by_name(buffer) != -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    std::string group_name = buffer;
    ICHIGO_INFO("New group: %s", group_name.c_str());

    // Step 3
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    // Step 4
    Util::IchigoVector<std::string> group_users;
    u32 user_count;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&user_count), sizeof(user_count)));
    ICHIGO_INFO("User count: %u", user_count);

    // Step 4
    bool failed = false;
    for (u32 i = 0; i < user_count; ++i) {
        RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&length), sizeof(length)));
        RETURN_IF_DROPPED(poll_recv(socket, buffer, length));
        buffer[length] = 0;

        ICHIGO_INFO("User: %s", buffer);

        i32 user_index = find_user_index_by_name(buffer);

        // Step 5
        if (user_index == -1)
            failed = true;
        else
            group_users.append(buffer);
    }

    if (!failed) {
        const Journal::NewGroupTransaction transaction(group_name, group_users);
        Journal::commit_transaction(&transaction);
        groups.append(Group(group_name, std::move(group_users)));
    }

    // Step 6
    buffer[0] = failed ? Error::INVALID_REQUEST : Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Attempt a login for a client.

    The flow between the server and the client is as follows:
    1. Receive the username to login as.
    2. Resolve this user. Send an ID of -1 and Error::INVALID_REQUEST if it cannot be resolved or if the user specified is already logged in.
    3. Send a login ID.
    4. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void login(u32 socket) {
    // Step 1
    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    // Step 2
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

    // Step 3
    send(socket, reinterpret_cast<char *>(&id), sizeof(id), 0);
    ICHIGO_INFO("User logged in: %s", buffer);
    // Step 4
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Attempt to logout for a client.

    The flow between the server and the client is as follows:
    1. Receive the ID of the user that is logging out.
    2. Resolve this user. Send Error::INVALID_REQUEST if it cannot be resolved or if the user specified is not logged in from this client.
    3. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void logout(u32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    // Step 2
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
    // Step 3
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Accept a parting message from a client and close the connection to it.

    The flow between the server and the client is as follows:
    1. Close the socket and clean up any socket specific data.

    Parameter 'socket': The client socket we are talking to.
*/
static void goodbye(u32 socket) {
    ICHIGO_INFO("Farewell socket %u", socket);

    u32 i = 0;
    for (; i < poll_connection_fds.size(); ++i) {
        if (poll_connection_fds.at(i).fd == socket)
            goto found_socket;
    }

    closesocket(socket);
    return;

found_socket:
    // Step 1
    poll_connection_fds.remove(i);
    connection_heartbeat_times.remove(i);
    closesocket(socket);
}

/*
    Accept a heartbeat from a client. Tells the server that this client is still alive.

    The flow between the server and the client is as follows:
    1. Check if we have heartbeat and poll data for this socket. If not, send Error::INVALID_REQUEST.
    2. Update the heartbeat time of this client to be now and send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void heartbeat(u32 socket) {
    // Step 1
    u32 i = 0;
    for (; i < poll_connection_fds.size(); ++i) {
        if (poll_connection_fds.at(i).fd == socket)
            goto success;
    }

    buffer[0] = Error::INVALID_REQUEST;
    send(socket, buffer, 1, 0);
    return;

    // Step 2
success:
    connection_heartbeat_times.at(i) = time(nullptr);
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Update the status of a user.

    The flow between the server and the client is as follows:
    1. Receive the ID of the logged in user.
    2. Resolve this user. If it is not logged in, was not found, or the socket fds do not match, send Error::INVALID_REQUEST.
    3. Receive the new status string.
    4. If the length of the string is too long, send Error::INVALID_REQUEST. Otherwise, send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void set_status(u32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    // Step 2
    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in() || users.at(index).connection_fd() != socket) {
        ICHIGO_INFO("User id=%d is not logged in, was not found, or is unauthorized to update status.", id);
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    // Step 3
    i32 n = poll_recv(socket, buffer, sizeof(buffer) - 1);
    RETURN_IF_DROPPED(n);

    buffer[n] = 0;

    // Step 4
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

/*
    Send a new message.

    The flow between the server and the client is as follows:
    1. Receive the ID of the logged in user.
    2. Resolve this user. If it is not logged in, was not found, or the socket fds do not match, send Error::INVALID_REQUEST.
    3. Receive the type of the recipient.
    4. Receive the name of the recipient.
    5. Receive the message content.
    6. If the length of the string is too long or the recipient cannot be found, send Error::INVALID_REQUEST. Otherwise, send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void send_message(i32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    // Step 2
    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in() || users.at(index).connection_fd() != socket) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    User *sender = &users.at(index);

    // Step 3
    // Receive the type of recipient (user or group)
    u8 recipient_type;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_type), sizeof(recipient_type)));

    // Step 4
    i32 message_id;
    i32 n;
    u32 recipient_name_size;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&recipient_name_size), sizeof(recipient_name_size)));
    RETURN_IF_DROPPED((n = poll_recv(socket, buffer, recipient_name_size)));
    buffer[n] = 0;

    std::string recipient_name = buffer;
    i32 recipient_index = recipient_type == RECIPIENT_TYPE_USER ? find_user_index_by_name(recipient_name) : find_group_index_by_name(recipient_name);

    // Step 5
    u32 message_size;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&message_size), sizeof(message_size)));
    RETURN_IF_DROPPED((n = poll_recv(socket, buffer, message_size)));
    buffer[n] = 0;

    // Step 6
    if (recipient_index == -1 || n > CHAT_MAX_MESSAGE_LENGTH) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    // Create the message(s)
    std::string message_content = buffer;
    message_id = get_next_id();

    if (recipient_type == RECIPIENT_TYPE_USER) {
        const Message message(message_content, &users.at(recipient_index), sender, message_id);
        const Journal::NewMessageTransaction transaction(message.sender()->name(), recipient_name, recipient_type, message.content());
        Journal::commit_transaction(&transaction);
        messages.append(message);
    } else {
        const Journal::NewMessageTransaction transaction(sender->name(), recipient_name, recipient_type, message_content);
        Journal::commit_transaction(&transaction);

        Group &group = groups.at(recipient_index);
        for (u32 i = 0; i < group.usernames().size(); ++i) {
            ICHIGO_INFO("Group message sending to %s with id %d", group.usernames().at(i).c_str(), message_id);
            recipient_index = find_user_index_by_name(group.usernames().at(i));
            assert(recipient_index != -1);
            const Message message(message_content, &users.at(recipient_index), sender, message_id);
            messages.append(message);

            // FIXME: This is a pointless commit at the end of the loop. Not a big deal, but just something to note.
            message_id = get_next_id();
        }
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Delete a message.

    The flow between the server and the client is as follows:
    1. Receive the ID of the logged in user.
    2. Resolve this user. If it is not logged in, was not found, or the socket fds do not match, send Error::INVALID_REQUEST and abort.
    3. Receive the message ID to delete.
    4. If the message cannot be found send Error::INVALID_REQUEST and abort.
    5. If the resolved message does not have the resolved user as a recipient, send Error::UNAUTHORIZED and abort.
    6. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void delete_message(i32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));

    // Step 2
    i32 user_index = find_user_index_by_id(id);
    if (user_index == -1 || !users.at(user_index).is_logged_in() || users.at(user_index).connection_fd() != socket) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    // Step 3
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), sizeof(id)));
    i32 message_index = find_message_index_by_id(id);

    // Step 4
    if (message_index == -1) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    // Step 5/6
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

/*
    Get all messages addressed for a user.

    The flow between the server and the client is as follows:
    1. Receive the ID of the logged in user.
    2. Resolve this user. If the user was not found, is not logged in, or the socket fds do not match, send Error::INVALID_REQUEST and abort.
    3. Send Error::SUCCESS.
    4. Send the number of messages addressed to the user provided.
    5. Send n message ID and content pairs (i32 and string).
    6. Send Error::SUCCESS.

    Parameter 'socket': The client socket we are talking to.
*/
static void get_messages(i32 socket) {
    // Step 1
    i32 id;
    RETURN_IF_DROPPED(poll_recv(socket, reinterpret_cast<char *>(&id), 4));

    // Step 2
    i32 index = find_user_index_by_id(id);
    if (index == -1 || !users.at(index).is_logged_in() || users.at(index).connection_fd() != socket) {
        buffer[0] = Error::INVALID_REQUEST;
        send(socket, buffer, 1, 0);
        return;
    }

    // Step 3
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);

    // Gather all messages that are addressed to this user
    const std::string &username = users.at(index).name();
    Util::IchigoVector<Message> messages_for_user;
    for (u32 i = 0; i < messages.size(); ++i) {
        if (messages.at(i).recipient()->usernames().index_of(username) != -1)
            messages_for_user.append(messages.at(i));
    }

    // Step 4
    u32 message_count = messages_for_user.size();
    send(socket, reinterpret_cast<char *>(&message_count), sizeof(message_count), 0);

    // Step 5
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

    // Step 6
    buffer[0] = Error::SUCCESS;
    send(socket, buffer, 1, 0);
}

/*
    Close all connections to sockets that have not sent Opcode::HEARTBEAT in more than 20 seconds.
    They are presumed to be dead at that point.
*/
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

/*
    Init and run server.
*/
void ChatServer::init() {
    // Initialize the journal with the default filename of "default.chatjournal".
    Journal::init("default.chatjournal");

    // Read all transactions from the journal file to rebuild the user, group, and message stores.
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
                    // The journal is expected to have updated the 'next id' through the 'UPDATE_ID' transaction before adding a new message
                    messages.append(Message(new_message_transaction->content(), recipient, &users.at(sender_index), next_id));
                } else if (new_message_transaction->recipient_type() == RECIPIENT_TYPE_GROUP) {
                    i32 group_index = find_group_index_by_name(new_message_transaction->recipient());
                    assert(group_index != -1);
                    Group &group = groups.at(group_index);
                    for (u32 i = 0; i < group.usernames().size(); ++i) {
                        i32 user_index = find_user_index_by_name(group.usernames().at(i));
                        assert(user_index != -1);
                        ICHIGO_INFO("Sending group message to %s content %s", users.at(user_index).name().c_str(), new_message_transaction->content().c_str());
                        messages.append(Message(new_message_transaction->content(), &users.at(user_index), &users.at(sender_index), next_id++));
                    }
                } else {
                    ICHIGO_ERROR("Invalid recipient type when reading new message from journal");
                    continue;
                }
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

    // Initialize winsock2
    [[maybe_unused]] WSADATA wsa_data;
    assert(WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0);

    // Listen on localhost port 8080
    u32 listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    InetPton(AF_INET, "127.0.0.1", &server_addr.sin_addr.S_un.S_addr);
    server_addr.sin_port = htons(8080);

    assert(bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) != SOCKET_ERROR);
    assert(listen(listen_fd, 10) != SOCKET_ERROR);

    // Set non-blocking IO mode so we can poll for new connections
    unsigned long imode = 1;
    ioctlsocket(listen_fd, FIONBIO, &imode);

    pollfd poll_listen_fd {
        .fd = listen_fd,
        .events = POLLRDNORM,
        .revents = 0,
    };

    // Main server event loop
    for (;;) {
        // Look for new connections
        i32 poll_result = WSAPoll(&poll_listen_fd, 1, 1);

        if (poll_result == SOCKET_ERROR) {
            ICHIGO_ERROR("Poll failed. Error code: %d", WSAGetLastError());
            break;
        }

        // Accept a new connection if one is being made and set up corresponding data
        if (poll_result > 0 && poll_listen_fd.revents & POLLRDNORM) {
            u32 connection_fd = accept(listen_fd, nullptr, nullptr);
            poll_connection_fds.append({ connection_fd, POLLRDNORM, 0 });
            connection_heartbeat_times.append(time(nullptr));

            ICHIGO_INFO("Accepted new connection");
        }

        // Check if any client has sent us new data to process.
        poll_result = WSAPoll(poll_connection_fds.data(), poll_connection_fds.size(), 1);

        if (poll_result > 0) {
            for (u32 i = 0; i < poll_connection_fds.size(); ++i) {
                if (poll_connection_fds.at(i).revents & POLLRDNORM) {
                    u32 connection_fd = poll_connection_fds.at(i).fd;

                    // Receive the opcode of the operation the client wishes to complete, then execute the corresponding conversation function.
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

        // Make sure to periodically check for dead connections.
        prune_dead_connections();
    }
}

/*
    Perform cleanup.
*/
void ChatServer::deinit() {
    Journal::deinit();
}
