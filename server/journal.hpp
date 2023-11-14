/*
    Server chat journal module. Provides an interface with the chat journal to the server module.
    Provides functions to read transactions to rebuild the state of the server, and functions to commit
    new transactions to the journal file.

    Author: Braeden Hong
      Date: November 11, 2023 - November 12, 2023
*/

#pragma once

#include "../common.hpp"
#include "chat_server.hpp"

namespace Journal {
    /*
        Enum defining the different types of transactions that can be committed.
    */
    enum class Operation {
        NEW_USER,
        NEW_MESSAGE,
        DELETE_MESSAGE,
        UPDATE_ID,
        NEW_GROUP,
    };

    /*
        The transaction interface. All types of transactions implement this.
    */
    struct Transaction {
        /*
            Get the type of operation that this transaction represents.
        */
        virtual Operation operation() const = 0;
        virtual ~Transaction() = default;
    };

    /*
        Transaction representing the creation of a new user.
        Implements Transaction.

        Contains the username of the new user.
    */
    class NewUserTransaction : public Transaction {
    public:
        explicit NewUserTransaction(const std::string &username) : m_username(username) {}
        Operation operation() const override { return Operation::NEW_USER; }
        const std::string &username() const { return m_username; }
    private:
        std::string m_username;
    };

    /*
        Transaction representing the creation of a new message.
        Implements Transaction.

        Contains the username of the sender, the name of the user or group that the message is being sent to,
        the type of recipient (user or group), and the content of the message.
    */
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

    /*
        Transaction representing the creation of a new group.
        Implements Transaction.

        Contains the name of the new group and a vector of all the usernames in the group.
    */
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

    /*
        Transaction representing the deletion of a message.
        Implements Transaction.

        Contains the ID of the message to delete.
    */
    class DeleteMessageTransaction : public Transaction {
    public:
        explicit DeleteMessageTransaction(u32 id) : m_id(id) {}
        Operation operation() const override { return Operation::DELETE_MESSAGE; }
        u32 id() const { return m_id; }
    private:
        u32 m_id;
    };

    /*
        Transaction representing the altering of the ID of the next transaction.
        Implements Transaction.

        Contains the ID of the next transaction.
    */
    class UpdateIdTransaction : public Transaction {
    public:
        explicit UpdateIdTransaction(u32 id) : m_id(id) {}
        Operation operation() const override { return Operation::UPDATE_ID; }
        u32 id() const { return m_id; }
    private:
        u32 m_id;
    };

    /*
        Initialize the journal module. Opens the journal file for reading/writing
        (creating if it does not exist), and calculates its filesize.

        Parameter 'journal_filename': The path to the journal file.
    */
    void init(const std::string &journal_filename);

    /*
        Closes the journal file.
    */
    void deinit();

    /*
        Commit a new transaction to the journal file. Can only be called after 'has_more_transactions()'
        returns false.

        Parameter 'transaction': The transaction to commit.
    */
    void commit_transaction(const Transaction *transaction);

    /*
        Read back the next transaction from the journal file. Can only be called when 'has_more_transactions()'
        returns true.
        Returns a pointer to the transaction read. Will be one of the specialized transactions listed above.
        This transaction must be returned by calling 'return_transaction()' to signify that it can be freed.
    */
    Transaction *next_transaction();

    /*
        Return the transaction received from 'next_transaction()' and free it from memory.
        Parameter 'transaction': The transaction to return.
    */
    void return_transaction(Transaction *transaction);

    /*
        Check if the file has any more transactions to read back. If not, new transactions may be committed.
        Returns whether or not the journal file has any unread transactions.
    */
    bool has_more_transactions();
}
