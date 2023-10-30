#pragma once
#include <string>

#include "util.hpp"
#include "recipient.hpp"
#include "user.hpp"

class Message {
public:
    Message() = default;
    Message(const std::string &message, Recipient *recipient, User *sender) : m_content(message), m_recipient(recipient), m_sender(sender) {}
    const std::string &content() const { return m_content; }
    const Recipient *recipient() const { return m_recipient; }
    const User *sender() const { return m_sender; }
private:
    std::string m_content;
    Recipient *m_recipient;
    User *m_sender;
};
