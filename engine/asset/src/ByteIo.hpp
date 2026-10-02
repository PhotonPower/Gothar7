#pragma once

// Internal: little-endian serialisation helpers for the binary asset formats (.g7pak, .g7mesh).

#include <g7/core/Types.hpp>

#include <bit>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset::detail
{
class ByteWriter
{
public:
    explicit ByteWriter(std::vector<u8>& out) : m_out(out) {}

    void u8v(u8 v) { m_out.push_back(v); }
    void u16v(u16 v) { put(v, 2); }
    void u32v(u32 v) { put(v, 4); }
    void u64v(u64 v) { put(v, 8); }
    void i32v(i32 v) { put(static_cast<u32>(v), 4); }
    void f32v(f32 v) { put(std::bit_cast<u32>(v), 4); }
    void bytes(std::span<const u8> data) { m_out.insert(m_out.end(), data.begin(), data.end()); }
    void text(std::string_view s) { m_out.insert(m_out.end(), s.begin(), s.end()); }
    /// u16 length + bytes; the caller guarantees s.size() <= 65535.
    void string16(std::string_view s)
    {
        u16v(static_cast<u16>(s.size()));
        text(s);
    }
    /// u32 length + bytes.
    void blob32(std::span<const u8> data)
    {
        u32v(static_cast<u32>(data.size()));
        bytes(data);
    }
    [[nodiscard]] usize size() const noexcept { return m_out.size(); }

private:
    void put(u64 v, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            m_out.push_back(static_cast<u8>(v >> (8 * i)));
        }
    }
    std::vector<u8>& m_out;
};

/// Bounds-checked reader. Reading past the end yields zeros and makes failed() sticky, so format
/// code can read a whole record and check once.
class ByteReader
{
public:
    explicit ByteReader(std::span<const u8> bytes) : m_bytes(bytes) {}

    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    [[nodiscard]] usize position() const noexcept { return m_pos; }
    [[nodiscard]] usize remaining() const noexcept { return m_bytes.size() - m_pos; }
    [[nodiscard]] bool has(u64 n) const noexcept { return !m_failed && n <= remaining(); }

    [[nodiscard]] u8 u8v() { return static_cast<u8>(get(1)); }
    [[nodiscard]] u16 u16v() { return static_cast<u16>(get(2)); }
    [[nodiscard]] u32 u32v() { return static_cast<u32>(get(4)); }
    [[nodiscard]] u64 u64v() { return get(8); }
    [[nodiscard]] i32 i32v() { return static_cast<i32>(static_cast<u32>(get(4))); }
    [[nodiscard]] f32 f32v() { return std::bit_cast<f32>(static_cast<u32>(get(4))); }

    [[nodiscard]] std::string_view text(usize n)
    {
        if (!take(n))
        {
            return {};
        }
        return {reinterpret_cast<const char*>(m_bytes.data() + m_pos - n), n};
    }
    [[nodiscard]] std::span<const u8> bytes(usize n)
    {
        if (!take(n))
        {
            return {};
        }
        return m_bytes.subspan(m_pos - n, n);
    }
    [[nodiscard]] std::string string16() { return std::string(text(u16v())); }
    [[nodiscard]] std::vector<u8> blob32()
    {
        const auto data = bytes(u32v());
        return {data.begin(), data.end()};
    }

private:
    bool take(usize n)
    {
        if (m_failed || n > remaining())
        {
            m_failed = true;
            return false;
        }
        m_pos += n;
        return true;
    }
    u64 get(usize n)
    {
        if (!take(n))
        {
            return 0;
        }
        u64 v = 0;
        for (usize i = 0; i < n; ++i)
        {
            v |= static_cast<u64>(m_bytes[m_pos - n + i]) << (8 * i);
        }
        return v;
    }

    std::span<const u8> m_bytes;
    usize m_pos = 0;
    bool m_failed = false;
};
} // namespace g7::asset::detail
