/*
    ClientMessage. A specialization of Message that implements client specific functions.
    Inherits from Message.

    Author: Braeden Hong
      Date: October 30, 2023 - November 12 2023
*/

#pragma once
#include "chat_client.hpp"
#include "../message.hpp"
#include "../group.hpp"

class ClientMessage : public Message {
public:
    ClientMessage() : Message() {}
    ClientMessage(const std::string &message, Recipient *recipient, User *sender) : Message(message, recipient, sender) {}
    ClientMessage(const std::string &message, Recipient *recipient, User *sender, i32 id) : Message(message, recipient, sender, id) {}

    /*
        Set whether or not the message has been read (currently unused)
    */
    void set_read(bool read) { m_read = read; }

    /*
        Send the message. The connection flow is outlined in the server connection header (server_connection.hpp)
        Parameter 'socket': The connection socket to the server.
        Parameter 'connection_id': The user ID of the logged in user.
        Returns whether or not the send succeeded.
    */
    bool send(i32 socket, i32 connection_id) {
        assert(socket != -1);

        u8 opcode = Opcode::SEND_MESSAGE;
        ::send(socket, reinterpret_cast<char *>(&opcode), sizeof(opcode), 0);
        ::send(socket, reinterpret_cast<char *>(&connection_id), sizeof(connection_id), 0);

        u8 result;
        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);

        if (result != Error::SUCCESS)
            return false;

        Util::IchigoVector<std::string> recipient_usernames = Message::recipient()->usernames();
        u8 recipient_type;
        std::string recipient_name;
        if (recipient_usernames.size() > 1) {
            recipient_type = RECIPIENT_TYPE_GROUP;
            recipient_name = static_cast<const Group *>(Message::recipient())->name();
        } else {
            recipient_type = RECIPIENT_TYPE_USER;
            recipient_name = Message::recipient()->usernames().at(0);
        }

        ::send(socket, reinterpret_cast<char *>(&recipient_type), sizeof(recipient_type), 0);

        u32 name_length = recipient_name.length();
        ::send(socket, reinterpret_cast<char *>(&name_length), sizeof(name_length), 0);
        ::send(socket, recipient_name.c_str(), recipient_name.length(), 0);

        u32 message_length = Message::content().length();
        ::send(socket, reinterpret_cast<char *>(&message_length), sizeof(message_length), 0);
        ::send(socket, Message::content().c_str(), Message::content().length(), 0);

        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);

        return result == Error::SUCCESS;
    }

    /*
        Delete the message. The connection flow is outlined in the server connection header (server_connection.hpp)
        Parameter 'socket': The connection socket to the server.
        Parameter 'connection_id': The user ID of the logged in user.
        Returns whether or not the delete succeeded.
    */
    bool delete_from_server(i32 socket, i32 connection_id) {
        assert(socket != -1);

        u8 opcode = Opcode::DELETE_MESSAGE;
        ::send(socket, reinterpret_cast<char *>(&opcode), sizeof(opcode), 0);
        ::send(socket, reinterpret_cast<char *>(&connection_id), sizeof(connection_id), 0);

        u8 result;
        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);

        if (result != Error::SUCCESS)
            return false;

        i32 message_id = id();
        ::send(socket, reinterpret_cast<char *>(&message_id), sizeof(message_id), 0);

        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);
        return result == Error::SUCCESS;
    }

    bool operator==(const ClientMessage &rhs) {
        return id() == rhs.id();
    }

private:
    bool m_read = false;
};
