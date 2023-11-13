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

void connect_to_server();
bool send_message(ClientMessage &message);
bool delete_message(ClientMessage &message);
bool set_status_of_logged_in_user(const std::string &status);
bool register_user(const std::string &username);
bool register_group(const std::string &name, const Util::IchigoVector<std::string> &usernames);
bool login(const std::string &username);
bool logout();
i32 refresh();
void deinit();
}
