#pragma once
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

#define VK_ASSERT_OK(err) assert(err == VK_SUCCESS)
#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))
#define ICHIGO_INFO(fmt, ...) std::printf("(info) %s:%d: " fmt "\n", __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)
#define ICHIGO_ERROR(fmt, ...) std::printf("(error) %s:%d: " fmt "\n", __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)

#define CHAT_MAX_STATUS_LENGTH 32
#define CHAT_MAX_MESSAGE_LENGTH 256

enum Opcode {
    SEND_MESSAGE,
    DELETE_MESSAGE,
    GET_MESSAGES,
    GET_USERS,
    SET_STATUS,
    LOGIN,
    LOGOUT,
    REGISTER,
    GOODBYE,
    HEARTBEAT,
};

enum Error {
    SUCCESS,
    INVALID_REQUEST,
    UNAUTHORIZED,
};

#ifdef _WIN32
#define platform_alloca _alloca
#endif
#ifdef _UNIX
#define platform_alloca alloca
#endif
