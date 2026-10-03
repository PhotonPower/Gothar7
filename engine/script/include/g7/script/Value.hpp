#pragma once

// Script values (M7): what crosses between Lua and the engine - nil, booleans, integers, numbers, strings,
// tables (array part + named fields) and references to Lua functions. No sol2 or Lua types in the public API
// (ADR 0006: sol2 stays private to the module).

#include <g7/core/Types.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace g7::script
{
/// A Lua function kept by the VM (dialog conditions, callbacks); valid until the scripts are reloaded.
struct FunctionRef
{
    u32 id = 0; ///< 0: none
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
    friend bool operator==(const FunctionRef&, const FunctionRef&) = default;
};

struct Table;

class Value
{
public:
    using Storage =
        std::variant<std::monostate, bool, i64, f64, std::string, std::shared_ptr<Table>, FunctionRef>;

    Value() = default;
    Value(bool v) : m_value(v) {}                              // NOLINT(google-explicit-constructor)
    Value(i64 v) : m_value(v) {}                               // NOLINT(google-explicit-constructor)
    Value(int v) : m_value(static_cast<i64>(v)) {}             // NOLINT(google-explicit-constructor)
    Value(f64 v) : m_value(v) {}                               // NOLINT(google-explicit-constructor)
    Value(std::string v) : m_value(std::move(v)) {}            // NOLINT(google-explicit-constructor)
    Value(const char* v) : m_value(std::string(v)) {}          // NOLINT(google-explicit-constructor)
    Value(std::shared_ptr<Table> v) : m_value(std::move(v)) {} // NOLINT(google-explicit-constructor)
    Value(FunctionRef v) : m_value(v) {}                       // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool isNil() const noexcept { return std::holds_alternative<std::monostate>(m_value); }
    [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(m_value); }
    [[nodiscard]] bool isInteger() const noexcept { return std::holds_alternative<i64>(m_value); }
    /// Integer or floating point.
    [[nodiscard]] bool isNumber() const noexcept
    {
        return isInteger() || std::holds_alternative<f64>(m_value);
    }
    [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(m_value); }
    [[nodiscard]] bool isTable() const noexcept
    {
        return std::holds_alternative<std::shared_ptr<Table>>(m_value);
    }
    [[nodiscard]] bool isFunction() const noexcept { return std::holds_alternative<FunctionRef>(m_value); }

    [[nodiscard]] bool asBool(bool fallback = false) const noexcept;
    [[nodiscard]] i64 asInteger(i64 fallback = 0) const noexcept;
    [[nodiscard]] f64 asNumber(f64 fallback = 0.0) const noexcept;
    [[nodiscard]] std::string_view asString() const noexcept; ///< empty for non-strings
    [[nodiscard]] const Table* asTable() const noexcept;      ///< nullptr for non-tables
    [[nodiscard]] FunctionRef asFunction() const noexcept;

    /// Field of a table (nil for missing fields and non-tables).
    [[nodiscard]] const Value& operator[](std::string_view field) const noexcept;
    /// "nil", "boolean", "integer", "number", "string", "table", "function" (as Lua names them, "integer"
    /// extra).
    [[nodiscard]] std::string_view typeName() const noexcept;
    /// Short readable form for the console and logs ("{1, 2, name = \"x\"}"), nested to a few levels.
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] const Storage& storage() const noexcept { return m_value; }
    friend bool operator==(const Value& a, const Value& b);

private:
    Storage m_value;
};

/// A Lua table: the sequence 1..n and the string-keyed fields (other keys are not carried over).
struct Table
{
    std::vector<Value> array;
    std::map<std::string, Value, std::less<>> fields;

    [[nodiscard]] const Value& field(std::string_view name) const noexcept;
    friend bool operator==(const Table&, const Table&) = default;
};

/// A new table value.
[[nodiscard]] Value makeTable(std::vector<Value> array = {},
                              std::map<std::string, Value, std::less<>> fields = {});
} // namespace g7::script
