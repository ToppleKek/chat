/*
    An interface representing someone or something that can accept messages.

    Author: Braeden Hong
      Date: October 30, 2023
*/

#pragma once
#include <string>
#include "util.hpp"

class Recipient {
public:
    virtual Util::IchigoVector<std::string> usernames() const = 0;
    virtual ~Recipient() {};
};
