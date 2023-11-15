/*
    A class representing a single message.

    Author: Braeden Hong
      Date: November 11, 2023
*/

#pragma once
#include <string>

#include "util.hpp"
#include "recipient.hpp"
#include "user.hpp"

class Message {
public:
    Message() = default;
    Message(const std::string &message, Recipient *recipient, User *sender)         : m_content(message), m_recipient(recipient), m_sender(sender) {}
    Message(const std::string &message, Recipient *recipient, User *sender, i32 id) : m_content(message), m_recipient(recipient), m_sender(sender), m_id(id) {}

    const std::string &content() const { return m_content; }
    const Recipient *recipient() const { return m_recipient; }
    const User *sender() const         { return m_sender; }
    i32 id() const                     { return m_id; }
    void set_id(i32 id)                { m_id = id; }
private:
    std::string m_content;
    Recipient *m_recipient;
    User *m_sender;
    i32 m_id = -1;
};
