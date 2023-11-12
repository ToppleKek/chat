#pragma once
#include "chat_client.hpp"
#include "../message.hpp"

class ClientMessage : public Message {
public:
    ClientMessage() : Message() {}
    ClientMessage(const std::string &message, Recipient *recipient, User *sender) : Message(message, recipient, sender) {}
    ClientMessage(const std::string &message, Recipient *recipient, User *sender, i32 id) : Message(message, recipient, sender, id) {}

    void set_read(bool read) { m_read = read; }
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
        u32 recipient_count = recipient_usernames.size();

        ::send(socket, reinterpret_cast<char *>(&recipient_count), sizeof(recipient_count), 0);
        for (u32 i = 0; i < recipient_count; ++i) {
            u32 name_length = recipient_usernames.at(i).length();
            ::send(socket, reinterpret_cast<char *>(&name_length), sizeof(name_length), 0);
            ::send(socket, recipient_usernames.at(i).c_str(), recipient_usernames.at(i).length(), 0);
        }

        u32 message_length = Message::content().length();
        ::send(socket, reinterpret_cast<char *>(&message_length), sizeof(message_length), 0);
        ::send(socket, Message::content().c_str(), Message::content().length(), 0);

        recv(socket, reinterpret_cast<char *>(&result), sizeof(result), 0);

        if (result != Error::SUCCESS)
            return false;


        i32 message_id;
        recv(socket, reinterpret_cast<char *>(&message_id), sizeof(message_id), 0);
        set_id(message_id);

        std::printf("got id %d\n", id());

        return true;
    }

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
