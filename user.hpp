#pragma once
#include <string>

#include "common.hpp"
#include "recipient.hpp"

class User {
public:
    User() = default;
    explicit User(const std::string &name) : m_name(name) {}

    const std::string &name() const { return m_name; }
    const std::string &status() const { return m_status; }
    void set_status(const std::string &status) { m_status = status; }
    void set_logged_in(bool logged_in) { m_logged_in = logged_in; }
    bool is_logged_in() const { return m_logged_in; }
    u32 last_heartbeat_time() const { return m_last_heartbeat_time; }
    void set_last_heartbeat_time(u32 last_heartbeat_time) { m_last_heartbeat_time = last_heartbeat_time; }
    i32 id() const { return m_id; }
    void set_id(i32 id) { m_id = id; }

private:
    std::string m_name;
    std::string m_status{"Offline"};
    bool m_logged_in = false;
    u32 m_last_heartbeat_time = 0;
    i32 m_id = -1;
};
