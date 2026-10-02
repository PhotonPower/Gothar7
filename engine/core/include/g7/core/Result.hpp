#pragma once

#include <g7/core/Assert.hpp>

#include <string>
#include <utility>
#include <variant>

namespace g7
{
/// Error value returned across module boundaries (see docs/04-coding-guidelines.md).
struct Error
{
    std::string message;
};

/// Minimal expected-like type. Engine code does not throw exceptions across module boundaries.
template <typename T>
class [[nodiscard]] Result
{
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : m_data(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return m_data.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() &
    {
        G7_ASSERT(ok(), "Result::value() called on error");
        return std::get<0>(m_data);
    }
    [[nodiscard]] const T& value() const&
    {
        G7_ASSERT(ok(), "Result::value() called on error");
        return std::get<0>(m_data);
    }
    [[nodiscard]] T&& value() &&
    {
        G7_ASSERT(ok(), "Result::value() called on error");
        return std::get<0>(std::move(m_data));
    }
    [[nodiscard]] const Error& error() const&
    {
        G7_ASSERT(!ok(), "Result::error() called on success");
        return std::get<1>(m_data);
    }

private:
    std::variant<T, Error> m_data;
};

/// Result without payload.
template <>
class [[nodiscard]] Result<void>
{
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)), m_ok(false) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const
    {
        G7_ASSERT(!ok(), "Result::error() called on success");
        return m_error;
    }

private:
    Error m_error;
    bool m_ok = true;
};
} // namespace g7
