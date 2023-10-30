#pragma once
#include "chat_client.hpp"
#include "../recipient.hpp"
#include "../message.hpp"

class ClientUser : public User, public Recipient {
public:
    ClientUser() : User() {}
    ClientUser(const std::string &name) : User(name) {}

    Util::IchigoVector<std::string> usernames() const override {
        Util::IchigoVector<std::string> ret(1);
        ret.append(name());
        return ret;
    }

    bool set_status(i32 socket, const std::string &status) {
        if (status.length() < 1)
            return false;

        assert(socket != -1);
        assert(this->id() != -1);

        u8 byte = Opcode::SET_STATUS;
        send(socket, reinterpret_cast<char *>(&byte), sizeof(byte), 0);

        i32 id = this->id();
        send(socket, reinterpret_cast<char *>(&id), 4, 0);

        i8 result;
        recv(socket, reinterpret_cast<char *>(&result), 1, 0);
        if (result != Error::SUCCESS) {
            return false;
        }

        send(socket, status.c_str(), status.length(), 0);
        recv(socket, reinterpret_cast<char *>(&result), 1, 0);

        return result == Error::SUCCESS;
    }
};
