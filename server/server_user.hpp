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

    i64  connection_fd() const { return m_connection_fd; }
    void set_connection_fd(i64 connection_fd) { m_connection_fd = connection_fd; }

private:
    i64 m_connection_fd = -1;
};
