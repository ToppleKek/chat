#pragma once

#include "../common.hpp"
#include "chat_server.hpp"

namespace Journal {
    enum class Operation {
        NEW_USER,
        NEW_MESSAGE,
        DELETE_MESSAGE,
        UPDATE_ID,
        NEW_GROUP,
    };

    struct Transaction {
        virtual Operation operation() const = 0;
        virtual ~Transaction() = default;
    };

    class NewUserTransaction : public Transaction {
    public:
        explicit NewUserTransaction(const std::string &username) : m_username(username) {}
        Operation operation() const override { return Operation::NEW_USER; }
        const std::string &username() const { return m_username; }
    private:
        std::string m_username;
    };

    class NewMessageTransaction : public Transaction {
    public:
        explicit NewMessageTransaction(const std::string &sender_username, const std::string &recipient, u32 recipient_type, const std::string &content) : m_sender(sender_username), m_recipient(recipient), m_recipient_type(recipient_type), m_content(content) {}
        Operation operation() const override { return Operation::NEW_MESSAGE; }
        const std::string &sender() const { return m_sender; }
        const std::string &recipient() const { return m_recipient; }
        u32 recipient_type() const { return m_recipient_type; }
        const std::string &content() const { return m_content; }
    private:
        std::string m_sender;
        std::string m_recipient;
        u32 m_recipient_type;
        std::string m_content;
    };

    class NewGroupTransaction : public Transaction {
    public:
        explicit NewGroupTransaction(const std::string &group_name, Util::IchigoVector<std::string> group_users) : m_group_name(group_name), m_group_users(group_users) {}
        Operation operation() const override { return Operation::NEW_GROUP; }
        const std::string &name() const { return m_group_name; }
        u32 user_count() const { return m_group_users.size(); }
        const Util::IchigoVector<std::string> &users() const { return m_group_users; }
    private:
        std::string m_group_name;
        Util::IchigoVector<std::string> m_group_users;
    };

    class DeleteMessageTransaction : public Transaction {
    public:
        explicit DeleteMessageTransaction(u32 id) : m_id(id) {}
        Operation operation() const override { return Operation::DELETE_MESSAGE; }
        u32 id() const { return m_id; }
    private:
        u32 m_id;
    };

    class UpdateIdTransaction : public Transaction {
    public:
        explicit UpdateIdTransaction(u32 id) : m_id(id) {}
        Operation operation() const override { return Operation::UPDATE_ID; }
        u32 id() const { return m_id; }
    private:
        u32 m_id;
    };

    void init(const std::string &journal_filename);
    void deinit();
    void commit_transaction(const Transaction *transaction);
    Transaction *next_transaction();
    void return_transaction(Transaction *transaction);
    bool has_more_transactions();
}
