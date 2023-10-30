#pragma once
#include <string>
#include "util.hpp"

class Recipient {
public:
    virtual Util::IchigoVector<std::string> usernames() const = 0;
    virtual ~Recipient() {};
};
