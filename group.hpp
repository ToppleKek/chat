#pragma once
#include "recipient.hpp"

class Group : public Recipient {
public:
    Group() = default;
    Group(const std::string &name, Util::IchigoVector<std::string> &&users) : m_name(name), m_users(users) {}
    Group(const std::string &name, const Util::IchigoVector<std::string> &users) : m_name(name), m_users(users) {}
    Util::IchigoVector<std::string> usernames() const override { return m_users; }
    const std::string &name() const { return m_name; }
private:
    std::string m_name;
    Util::IchigoVector<std::string> m_users;
};
