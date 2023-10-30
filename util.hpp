#pragma once
#include "common.hpp"
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cstdio>

namespace Util {
inline u32 clamp(u32 value, u32 min, u32 max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

template<typename T>
class IchigoVector {
public:
    IchigoVector(u64 initial_capacity) : m_capacity(initial_capacity), m_data(new T[initial_capacity]) {}
    IchigoVector() : IchigoVector(16) {}
    IchigoVector(const IchigoVector<T> &other) { operator=(other); }
    IchigoVector(IchigoVector<T> &&other) : m_capacity(other.m_capacity), m_size(other.m_size), m_data(other.m_data) { other.m_data = nullptr; other.m_capacity = 0; other.m_size = 0; }
    IchigoVector &operator=(const IchigoVector<T> &other) {
        m_capacity = other.m_capacity;
        m_size = other.m_size;
        std::memcpy(m_data, other.m_data, m_size * sizeof(T));
    }
    ~IchigoVector() { if (m_data) delete[] m_data; }

    T &at(u64 i) { return m_data[i]; }
    const T &at(u64 i) const { assert(i < m_size); return m_data[i]; }
    const T *data() const { return m_data; }
    T *release_data() { T *ret = m_data; m_data = new T[16]; m_capacity = 16; m_size = 0; return ret; }
    u64 size() const { return m_size; }
    void clear() { m_size = 0; }

    void insert(u64 i, T item) {
        assert(i <= m_size);

        if (m_size == m_capacity)
            expand();

        if (i == m_size) {
            m_data[m_size++] = item;
            return;
        }

        std::memmove(&m_data[i + 1], &m_data[i], (m_size - i) * sizeof(T));
        m_data[i] = item;
        ++m_size;
    }

    u64 append(T item) {
        if (m_size == m_capacity)
            expand();

        m_data[m_size++] = item;
        return m_size - 1;
    }

    T remove(u64 i) {
        assert(i < m_size);

        if (i == m_size - 1)
            return m_data[--m_size];

        T ret = m_data[i];
        std::memmove(&m_data[i], &m_data[i + 1], (m_size - i - 1) * sizeof(T));
        --m_size;
        return ret;
    }

    i64 index_of(const T &item) const {
        for (i64 i = 0; i < m_size; ++i) {
            if (m_data[i] == item)
                return i;
        }

        return -1;
    }

    void resize(u64 size) {
        assert(size >= m_size);
        T *new_data = new T[size];

        for (u64 i = 0; i < m_size; ++i)
            new_data[i] = m_data[i];

        delete[] m_data;
        m_data = new_data;
        m_capacity = size;
    }


private:
    u64 m_capacity;
    u64 m_size = 0;
    T *m_data;

    void expand() {
        T *new_data = new T[m_capacity *= 2];

        for (u64 i = 0; i < m_size; ++i)
            new_data[i] = m_data[i];

        delete[] m_data;
        m_data = new_data;
    }
};
}
