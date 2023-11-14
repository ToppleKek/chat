/*
    Server connection module. Handles all server operations including connecting/disconnecting, fetching users/messages/groups, sending messages, etc.

    Globals:
    cached_users: A vector of users stored after the last heartbeat to the server
    cached_groups: A vector of groups stored after the last heartbeat to the server
    cached_inbox: A vector of messages that have the logged_in_user in the recipient field stored after the last heartbeat to the server
    cached_outbox: A vector of all messages sent during this client session
    logged_in_user: The ClientUser of the currently logged in user

    Author: Braeden Hong
      Date: November 5, 2023 - November 12 2023
*/

#pragma once
#include "../common.hpp"
#include "../util.hpp"
#include "client_user.hpp"
#include "client_message.hpp"
#include "../group.hpp"
#include <string>

namespace ServerConnection {
extern Util::IchigoVector<ClientUser> cached_users;
extern Util::IchigoVector<Group> cached_groups;
extern Util::IchigoVector<ClientMessage> cached_inbox;
extern Util::IchigoVector<ClientMessage> cached_outbox;
extern ClientUser logged_in_user;

/*
    Connect to the server. Establishes a TCP socket connection and starts the heartbeat thread.
*/
void connect_to_server();

/*
    Send a message to the server.

    The flow between the client and server is as follows:
    1. Send SEND_MESSAGE opcode.
    2. Send user ID of the logged in user.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Send the recipient type (user/group).
    5. Send the name of the recipient (user/group). As with all string communication,
       first send the length of the string, and then 'length' characters.
    6. Send the message content string (following string sending conventions)
    7. Receive a result.

    Parameter 'message': The message to be sent
    Returns whether or not the send was successful
*/
bool send_message(ClientMessage &message);

/*
    Delete a message from the server.

    The flow between the client and server is as follows:
    1. Send DELETE_MESSAGE opcode.
    2. Send user ID of the logged in user.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Send the ID of the message to be deleted.
    5. Receive a result.

    Parameter 'message': The message to be deleted
    Returns whether or not the deletion was successful
*/
bool delete_message(ClientMessage &message);

/*
    Set the status of the currently logged in user.

    The flow between the client and server is as follows:
    1. Send SET_STATUS opcode.
    2. Send user ID of the logged in user.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Send the new status string.
    5. Receive a result.

    Parameter 'status': The new status
    Returns whether or not the status update was successful
*/
bool set_status_of_logged_in_user(const std::string &status);

/*
    Register a new user with the server.

    The flow between the client and server is as follows:
    1. Send REGISTER_GROUP opcode.
    2. Send the name of the group to register as a string.
    3. Receive a result.

    Parameter 'username': The username of the new user
    Returns whether or not the registration was successful
*/
bool register_user(const std::string &username);

/*
    Register a new group with the server.

    The flow between the client and server is as follows:
    1. Send REGISTER opcode.
    2. Send the username of the user to register as a string.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Send the number of users in the group
    5. Send n username strings to the server
    6. Receive a result.

    Parameter 'name': The name of the group
    Parameter 'usernames': A vector of usernames of the users included in this group
    Returns whether or not the registration was successful
*/
bool register_group(const std::string &name, const Util::IchigoVector<std::string> &usernames);

/*
    Login.

    The flow between the client and server is as follows:
    1. Send LOGIN opcode.
    2. Send the username string of the user to login as.
    3. Receive a unique ID. This is the ID of the logged in user.
    4. Receive a result.

    Parameter 'username': The name of the user to login as
    Returns whether or not the login attempt was successful
*/
bool login(const std::string &username);

/*
    Logout.

    The flow between the client and server is as follows:
    1. Send LOGOUT opcode.
    2. Send the ID of the user to logout. (Connection ID is checked to ensure that you cannot logout anyone besides yourself.)
    3. Receive a result.

    Returns whether or not the logout attempt was successful
*/
bool logout();

/*
    Refresh cached vectors (users, groups, messages).

    The flow between the client and server for getting USERS is as follows:
    1. Send GET_USERS opcode.
    2. Send the ID of the logged in user.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Receive the number of users.
    5. Receive n username and status pairs (2 strings).
    6. Receive a result.

    The flow between the client and server for getting GROUPS is as follows:
    1. Send GET_GROUPS opcode.
    2. Send the ID of the logged in user.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Receive the number of groups. (n)
    5. Receive the group name (string).
    6. Receive n groups by doing the following:
        6a. Receive the number of users in the group. (m)
        6b. Receive m usernames. These users are in the group.
    7. Receive a result.

    The flow between the client and server for getting MESSAGES is as follows:
    1. Send GET_MESSAGES opcode.
    2. Send the ID of the logged in user.
    3. Receive a result from the server. If the result is Error::SUCCESS, proceed.
       If it is not abort.
    4. Receive the number of messages for the logged in user.
    5. Receive n message ID and content pairs (i32 and string).
    6. Receive a result.

    Returns the number of new messages (used to determine if the new message popup must be shown)
*/
i32 refresh();

/*
    Close the connection to the server.

    The flow between the client and server is as follows:
    1. Send GOODBYE opcode. (This is to let the server know that it can close the connection on its end.)
    2. Close the connection socket.
*/
void deinit();
}
