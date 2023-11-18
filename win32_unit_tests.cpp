#include <cstdio>

#include "client/server_connection.hpp"
#include "client/client_message.hpp"
#include "common.hpp"

u32 index_ = 0;
u32 success_count_ = 0;

#define TEST(B, DESC)                                                \
    do {                                                             \
        if (B) {                                                     \
            ++success_count_;                                        \
            std::printf("Test: (%2u) SUCCESS: " #DESC "\n", index_); \
        } else {                                                     \
            std::printf("Test: (%2u) FAILURE: " #DESC "\n", index_); \
        }                                                            \
        ++index_;                                                    \
    } while (0)

i32 main() {
    SECURITY_ATTRIBUTES security_attr{};
    security_attr.nLength        = sizeof(SECURITY_ATTRIBUTES);
    security_attr.bInheritHandle = true;

    HANDLE server_output_file = CreateFile("server_log.txt", FILE_APPEND_DATA, FILE_SHARE_WRITE | FILE_SHARE_READ, &security_attr, OPEN_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);

    DeleteFile("unit_test_backup.chatjournal");
    MoveFile("default.chatjournal", "unit_test_backup.chatjournal");

    PROCESS_INFORMATION pi{};

    STARTUPINFO si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = server_output_file;
    si.hStdError  = server_output_file;

    assert(CreateProcessA(nullptr, "chat.exe", nullptr, nullptr, true, 0, nullptr,
                        nullptr, &si, &pi));

    // Wait a bit to ensure the server has started.
    Sleep(500);

    ServerConnection::connect_to_server();

    // ** Test user registration **
    TEST(ServerConnection::register_user("unit_test"), "Register a new user");
    TEST(ServerConnection::register_user("unit_test_2"), "Register another new user");
    // Should fail (return false) because we just registered that user
    TEST(!ServerConnection::register_user("unit_test"), "Register an existing user");

    // ** Login **
    TEST(!ServerConnection::login("non_existant_user"), "Login as a non-existant user");
    TEST(ServerConnection::login("unit_test"), "Login as the registered user");

    // ** Refresh **
    TEST(ServerConnection::refresh() == 0, "Refresh data, should be 0 new messages");

    // ** Message sending **
    ClientMessage msg("test", &ServerConnection::logged_in_user, &ServerConnection::logged_in_user);
    ClientMessage long_msg("This is a very long message that has too many characters to fit inside of CHAT_MAX_MESSAGE_LENGTH."
                            "Therefore, when we send it to the server, we should get an error. AbcdefghijklmnopqrstuvwxyzAbcdefghijklmnopqrstuvwxyzAbcdefghijklmnopqrstuvwxyzAbcdefghijklmnopqrstuvwxyz",
                            &ServerConnection::cached_users.at(0), &ServerConnection::logged_in_user);

    TEST(ServerConnection::send_message(msg), "Send a message from the logged in user to ourselves");
    TEST(!ServerConnection::send_message(long_msg), "Send a message that is too long");

    // ** Refresh after sending messages **
    TEST(ServerConnection::refresh() == 1, "Refresh data, should be 1 new message since we just sent one to ourselves");

    // ** Status update **
    TEST(ServerConnection::set_status_of_logged_in_user("test status"), "Update status of logged in user");
    TEST(!ServerConnection::set_status_of_logged_in_user("This status is too long to fit in CHAT_MAX_STATUS_LENGTH and should be rejected"), "Update status with a message that is too long");

    // ** Groups **
    Util::IchigoVector<std::string> usernames;
    usernames.append("unit_test");
    usernames.append("unit_test_2");
    TEST(ServerConnection::register_group("test group", usernames), "Create a new group with both users");
    TEST(!ServerConnection::register_group("test group", usernames), "Attempt to create the same group");
    usernames.append("non_existant_user");
    TEST(!ServerConnection::register_group("invalid group", usernames), "Attempt to create a group with an invalid user");
    ServerConnection::refresh();
    TEST(ServerConnection::cached_groups.size() == 1, "One group fetched after group creation");

    // ** Group messaging **
    ClientMessage group_msg("test group message", &ServerConnection::cached_groups.at(0), &ServerConnection::logged_in_user);
    TEST(ServerConnection::send_message(group_msg), "Send a group message");
    TEST(ServerConnection::refresh() == 1 && ServerConnection::cached_inbox.size() == 2, "Receive group message");

    // ** Delete a message **
    TEST(ServerConnection::delete_message(ServerConnection::cached_inbox.at(0)), "Delete the first message in the inbox");

    // ** Logout, and login as the other user **
    TEST(ServerConnection::logout(), "Log out");
    TEST(ServerConnection::login("unit_test_2"), "Login as the second created user");

    // ** Receive messages on the other user **
    TEST(ServerConnection::refresh() == 1, "Receive the group message on the second user");
    TEST(ServerConnection::logout(), "Log out of the second user");

    ServerConnection::deinit();
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(server_output_file);

    DeleteFile("unit_test_result.chatjournal");
    MoveFile("default.chatjournal", "unit_test_result.chatjournal");
    MoveFile("unit_test_backup.chatjournal", "default.chatjournal");

    std::printf("Tests completed. %u failed, %u succeeded\n", index_ - success_count_, success_count_);
    return 0;
}
