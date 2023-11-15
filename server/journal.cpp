/*
    Server chat journal module implementation. See header (journal.hpp) for public function documentation.

    Author: Braeden Hong
      Date: November 11, 2023 - November 12, 2023
*/

#include "journal.hpp"
#include "../util.hpp"
#include <optional>

// The journal file that is in use
static std::FILE *journal_file   = nullptr;
// The size of said file
static i32 journal_file_size     = 0;
// If this is set, no transactions can be read back from the file or committed to the file
static bool invalid_file         = false;

#define INVALID_U32 static_cast<u32>(~0)

/*
    Get the next non-whitespace character from the file.
    Returns the next non-whitespace character of 'journal_file'.
*/
static inline char next_non_whitespace() {
    char c;
    while (std::isspace(c = std::fgetc(journal_file)));
    return c;
}

/*
    Read an unsigned 32-bit integer from the current position in the journal file.
    Returns an unsigned 32-bit integer parsed (base 10) from the current position in the file, or INVALID_U32 if no number could be parsed.
*/
static u32 read_u32() {
    static char buffer[1024];
    buffer[0] = next_non_whitespace();

    for (u32 i = 1; i < 1023; ++i) {
        buffer[i] = std::fgetc(journal_file);
        if (std::isspace(buffer[i]) || buffer[i] == EOF) {
            buffer[i] = 0;
            char *end;
            u32 number = std::strtol(buffer, &end, 10);
            if (buffer == end) {
                ICHIGO_ERROR("Failed to parse u32 due to invalid number format");
                return INVALID_U32;
            }

            return number;
        }
    }

    ICHIGO_ERROR("Failed to parse u32");
    return INVALID_U32;
}

/*
    Read a string surrounded by quotes from the current position in the journal file.
    Returns an optional that contains either the string parsed (without the quotes) or no value if a string could not be parsed.
*/
static std::optional<std::string> read_quoted_string() {
    static char buffer[1024];

    if (next_non_whitespace() != '"') {
        ICHIGO_ERROR("Expected \" to begin string");
        return {};
    }

    for (u32 i = 0; i < 1023; ++i) {
        buffer[i] = std::fgetc(journal_file);
        if (buffer[i] == '"') {
            buffer[i] = 0;
            return buffer;
        }
    }

    ICHIGO_ERROR("String too long");
    return {};
}

void Journal::init(const std::string &journal_filename) {
    journal_file = ChatServer::platform_open_file(journal_filename, ChatServer::platform_file_exists(journal_filename.c_str()) ? "r+b" : "w+b");
    std::fseek(journal_file, 0, SEEK_END);
    journal_file_size = std::ftell(journal_file);
    std::fseek(journal_file, 0, SEEK_SET);
    ICHIGO_INFO("Journal file loaded: size is %u\n", journal_file_size);
}

void Journal::deinit() {
    std::fclose(journal_file);
}

void Journal::commit_transaction(const Transaction *transaction) {
    static char buffer[1024];

    if (invalid_file) {
        ICHIGO_ERROR("Invalid journal file provided: the server is operating without a journal!");
        return;
    }

    assert(!Journal::has_more_transactions());

    std::fputc('\n', journal_file);
    switch (transaction->operation()) {
        case Journal::Operation::NEW_USER: {
            const NewUserTransaction *new_user_transaction = static_cast<const NewUserTransaction *>(transaction);
            std::snprintf(buffer, sizeof(buffer), "NEW_USER \"%s\"", new_user_transaction->username().c_str());
            std::fwrite(buffer, sizeof(char), std::strlen(buffer), journal_file);
        } break;
        case Journal::Operation::NEW_MESSAGE: {
            // Format: NEW_MESSAGE "sender username" recipient_type "recipient name" "message content"
            const NewMessageTransaction *new_message_transaction = static_cast<const NewMessageTransaction *>(transaction);
            std::snprintf(
                buffer,
                sizeof(buffer),
                "NEW_MESSAGE \"%s\" %u \"%s\" \"%s\"",
                new_message_transaction->sender().c_str(),
                new_message_transaction->recipient_type(),
                new_message_transaction->recipient().c_str(),
                new_message_transaction->content().c_str()
            );
            std::fwrite(buffer, sizeof(char), std::strlen(buffer), journal_file);
        } break;
        case Journal::Operation::DELETE_MESSAGE: {
            // Format: DELETE_MESSAGE message_id
            const DeleteMessageTransaction *delete_message_transaction = static_cast<const DeleteMessageTransaction *>(transaction);
            std::snprintf(buffer, sizeof(buffer), "DELETE_MESSAGE %u", delete_message_transaction->id());
            std::fwrite(buffer, sizeof(char), std::strlen(buffer), journal_file);
        } break;
        case Journal::Operation::UPDATE_ID: {
            // Format: UPDATE_ID new_id
            const UpdateIdTransaction *update_id_transaction = static_cast<const UpdateIdTransaction *>(transaction);
            std::snprintf(buffer, sizeof(buffer), "UPDATE_ID %u", update_id_transaction->id());
            std::fwrite(buffer, sizeof(char), std::strlen(buffer), journal_file);
        } break;
        case Journal::Operation::NEW_GROUP: {
            // Format: NEW_GROUP "group name" member_count "username" "username" ...(member_count times)
            const NewGroupTransaction *new_group_transaction = static_cast<const NewGroupTransaction *>(transaction);
            std::snprintf(buffer, sizeof(buffer), "NEW_GROUP \"%s\" %u ", new_group_transaction->name().c_str(), new_group_transaction->user_count());

            const auto &users = new_group_transaction->users();
            for (u32 i = 0; i < users.size(); ++i) {
                std::strncat(buffer, "\"", 1);
                std::strncat(buffer, users.at(i).c_str(), users.at(i).length());
                std::strncat(buffer, "\" ", 2);
            }

            ICHIGO_INFO("New group string: %s", buffer);
            std::fwrite(buffer, sizeof(char), std::strlen(buffer), journal_file);
        } break;
    }

    std::fflush(journal_file);
}

Journal::Transaction *Journal::next_transaction() {
    if (invalid_file) {
        ICHIGO_ERROR("Invalid journal file provided: the server is operating without a journal!");
        return nullptr;
    }

    static char buffer[1024];
    buffer[0] = next_non_whitespace();
    // Get the transaction operation
    for (u32 i = 1; i < 1023; ++i) {
        buffer[i] = std::fgetc(journal_file);
        if (std::isspace(buffer[i])) {
            buffer[i] = 0;
            break;
        }
    }

    if (std::strcmp(buffer, "NEW_USER") == 0) {
        auto username = read_quoted_string();
        if (!username.has_value())
            goto fail;

        return new NewUserTransaction(username.value());
    } else if (std::strcmp(buffer, "UPDATE_ID") == 0) {
        u32 id = read_u32();

        if (id == INVALID_U32) {
            ICHIGO_ERROR("Invalid u32");
            goto fail;
        }

        return new UpdateIdTransaction(id);
    } else if (std::strcmp(buffer, "NEW_MESSAGE") == 0) {
        auto sender = read_quoted_string();

        if (!sender.has_value())
            goto fail;

        u32 recipient_type = read_u32();

        if (recipient_type == INVALID_U32) {
            goto fail;
        }

        auto recipient = read_quoted_string();

        if (!recipient.has_value())
            goto fail;

        auto content = read_quoted_string();

        if (!content.has_value())
            goto fail;

        return new NewMessageTransaction(sender.value(), recipient.value(), recipient_type, content.value());
    } else if (std::strcmp(buffer, "DELETE_MESSAGE") == 0) {
        u32 id = read_u32();

        if (id == INVALID_U32)
            goto fail;

        return new DeleteMessageTransaction(id);
    } else if (std::strcmp(buffer, "NEW_GROUP") == 0) {
        auto name = read_quoted_string();

        if (!name.has_value())
            goto fail;

        // ICHIGO_INFO("Read group name: %s", )

        u32 user_count = read_u32();

        if (user_count == INVALID_U32)
            goto fail;

        Util::IchigoVector<std::string> users;
        for (u32 i = 0; i < user_count; ++i) {
            auto username = read_quoted_string();
            if (!username.has_value())
                goto fail;

            users.append(username.value());
        }

        return new NewGroupTransaction(name.value(), users);
    }

fail:
        invalid_file = true;
        return nullptr;
}

void Journal::return_transaction(Transaction *transaction) {
    delete transaction;
}

bool Journal::has_more_transactions() {
    if (invalid_file) {
        ICHIGO_ERROR("Invalid journal file provided: the server is operating without a journal!");
        return false;
    }

    // Consume whitespace at beginning of line
    u32 pos = std::ftell(journal_file);

    for (char c = std::fgetc(journal_file);; c = std::fgetc(journal_file)) {
        if (c == EOF)
            return false;

        if (!std::isspace(c))
            break;
    }

    std::fseek(journal_file, pos, SEEK_SET);
    return true;
}
