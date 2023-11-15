/*
    A class representing a group of users. Implements the Recipient interface.

    Author: Braeden Hong
      Date: November 11, 2023
*/

#pragma once
#include "recipient.hpp"

class Group : public Recipient {
public:
    Group() = default;
    // Specialization for allowing the vector of usernames to be moved instead of copied to the group class.
    Group(const std::string &name, Util::IchigoVector<std::string> &&users)      : m_name(name), m_users(users) {}
    Group(const std::string &name, const Util::IchigoVector<std::string> &users) : m_name(name), m_users(users) {}

    Util::IchigoVector<std::string> usernames() const override { return m_users; }
    const std::string &name() const                            { return m_name; }
private:
    std::string m_name;
    Util::IchigoVector<std::string> m_users;
};
