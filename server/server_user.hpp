/*
    ServerUser class. A specialization of User that implements server specific functions.
    Inherits from User. Implements the Recipient interface.

    Author: Braeden Hong
      Date: October 30, 2023 - November 12 2023
*/

#pragma once
#include "chat_server.hpp"
#include "../user.hpp"
#include "../recipient.hpp"

class ServerUser : public User, public Recipient {
public:
    ServerUser() : User() {}
    ServerUser(const std::string &name) : User(name) {}

    Util::IchigoVector<std::string> usernames() const override {
        Util::IchigoVector<std::string> ret(1);
        ret.append(name());
        return ret;
    }

    // Server specific getter/setter for the socket connection file descriptor
    i64  connection_fd() const                { return m_connection_fd; }
    void set_connection_fd(i64 connection_fd) { m_connection_fd = connection_fd; }

private:
    i64 m_connection_fd = -1;
};
