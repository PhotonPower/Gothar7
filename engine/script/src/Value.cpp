#include <g7/script/Value.hpp>

#include <format>

namespace g7::script
{
namespace
{
const Value kNil;

void append(std::string& out, const Value& v, int depth)
{
    if (const Table* t = v.asTable())
    {
        if (depth >= 3)
        {
            out += "{...}";
            return;
        }
        out += '{';
        bool first = true;
        for (const Value& item : t->array)
        {
            out += first ? "" : ", ";
            append(out, item, depth + 1);
            first = false;
        }
        for (const auto& [name, item] : t->fields)
        {
            out += first ? "" : ", ";
            out += name + " = ";
            append(out, item, depth + 1);
            first = false;
        }
        out += '}';
        return;
    }
    if (v.isString())
    {
        out += std::format("\"{}\"", v.asString());
        return;
    }
    out += v.toString();
}
} // namespace

bool Value::asBool(bool fallback) const noexcept
{
    return isBool() ? std::get<bool>(m_value) : fallback;
}

i64 Value::asInteger(i64 fallback) const noexcept
{
    if (isInteger())
    {
        return std::get<i64>(m_value);
    }
    return std::holds_alternative<f64>(m_value) ? static_cast<i64>(std::get<f64>(m_value)) : fallback;
}

f64 Value::asNumber(f64 fallback) const noexcept
{
    if (isInteger())
    {
        return static_cast<f64>(std::get<i64>(m_value));
    }
    return std::holds_alternative<f64>(m_value) ? std::get<f64>(m_value) : fallback;
}

std::string_view Value::asString() const noexcept
{
    return isString() ? std::string_view(std::get<std::string>(m_value)) : std::string_view();
}

const Table* Value::asTable() const noexcept
{
    return isTable() ? std::get<std::shared_ptr<Table>>(m_value).get() : nullptr;
}

FunctionRef Value::asFunction() const noexcept
{
    return isFunction() ? std::get<FunctionRef>(m_value) : FunctionRef{};
}

const Value& Value::operator[](std::string_view field) const noexcept
{
    const Table* t = asTable();
    return t ? t->field(field) : kNil;
}

std::string_view Value::typeName() const noexcept
{
    constexpr std::string_view kNames[] = {"nil",    "boolean", "integer", "number",
                                           "string", "table",   "function"};
    return kNames[m_value.index()];
}

std::string Value::toString() const
{
    switch (m_value.index())
    {
    case 0:
        return "nil";
    case 1:
        return asBool() ? "true" : "false";
    case 2:
        return std::to_string(asInteger());
    case 3:
        return std::format("{}", asNumber());
    case 4:
        return std::string(asString());
    case 5:
    {
        std::string out;
        append(out, *this, 0);
        return out;
    }
    default:
        return std::format("function #{}", asFunction().id);
    }
}

bool operator==(const Value& a, const Value& b)
{
    if (a.isTable() && b.isTable())
    {
        return *a.asTable() == *b.asTable();
    }
    if (a.isNumber() && b.isNumber() && a.isInteger() != b.isInteger())
    {
        return a.asNumber() == b.asNumber();
    }
    return a.m_value == b.m_value;
}

const Value& Table::field(std::string_view name) const noexcept
{
    const auto it = fields.find(name);
    return it == fields.end() ? kNil : it->second;
}

Value makeTable(std::vector<Value> array, std::map<std::string, Value, std::less<>> fields)
{
    auto t = std::make_shared<Table>();
    t->array = std::move(array);
    t->fields = std::move(fields);
    return Value(std::move(t));
}
} // namespace g7::script
