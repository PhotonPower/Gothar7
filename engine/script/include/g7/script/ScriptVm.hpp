#pragma once

// The Lua 5.4 VM (M7, ADR 0006): sandboxed, loads game/scripts in a fixed order, collects declarative
// instances
// (`Item "it_sword_old" { ... }`) and checks them against their kind's schema. Lua and sol2 stay private
// (PImpl). Spec: docs/modules/script.md.

#include <g7/core/Result.hpp>
#include <g7/script/Value.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::script
{
struct ScriptConfig
{
    /// Reads a script by its path relative to the script root, e.g. "items/weapons.lua" (runtime: the VFS).
    std::function<Result<std::string>(std::string_view path)> readFile;
    /// Every .lua file below the root, as relative paths with '/'.
    std::function<std::vector<std::string>()> listFiles;
    /// print() output; default: the log (channel "script").
    std::function<void(std::string_view)> print;
    u64 instructionLimit = 50'000'000; ///< per run of a file, console line or call (endless loops end)
    usize memoryLimit = usize{256} << 20;
};

/// A problem in the scripts: where (file and line, when known) and what.
struct ScriptError
{
    std::string file;
    i32 line = 0;
    std::string message;
    [[nodiscard]] std::string text() const; ///< "items/weapons.lua:3: message"
};

/// One field of an instance kind.
struct FieldSpec
{
    enum class Type : u8
    {
        Any,
        Boolean,
        Integer,
        Number, ///< integer or floating point
        String,
        StringList, ///< { "a", "b" }
        Table,
        Function,
    };
    std::string name;
    Type type = Type::Any;
    bool required = false;
    std::optional<f64> min; ///< numbers
    std::optional<f64> max;
    /// The value (or each entry of a StringList) must name an instance of this kind ("Item" ...).
    std::string refKind;
};

/// An instance constructor in Lua, `<name> "<instance>" { fields }`, with the schema its tables must follow.
struct InstanceKind
{
    std::string name; ///< "Item", "Npc", "Info", "Quest", "Routine"
    std::vector<FieldSpec> fields;
    bool allowUnknownFields = false;
};

struct Instance
{
    std::string kind;
    std::string name;
    Value fields; ///< the table
    std::string file;
    i32 line = 0;
};

class ScriptVm
{
public:
    [[nodiscard]] static Result<ScriptVm> create(ScriptConfig config);

    ScriptVm();
    ~ScriptVm();
    ScriptVm(ScriptVm&&) noexcept;
    ScriptVm& operator=(ScriptVm&&) noexcept;

    /// Declares an instance kind (before loadAll): a global function `<name>` in Lua.
    void defineKind(InstanceKind kind);
    /// Runs every script once: lib/ first, then data/, then the rest; alphabetically within. An error stops
    /// only its file. Afterwards every instance is checked against its kind (types, ranges, required fields,
    /// references). Returns the number of files that ran without error; errors() lists the problems.
    usize loadAll();

    /// Runs a piece of Lua (console, tests); its result (the first return value).
    [[nodiscard]] Result<Value> runString(std::string_view code, std::string_view chunkName = "console");
    /// Calls a kept Lua function (dialog conditions, callbacks) or a global one.
    [[nodiscard]] Result<Value> call(FunctionRef function, std::span<const Value> arguments = {});
    [[nodiscard]] Result<Value> callGlobal(std::string_view name, std::span<const Value> arguments = {});
    [[nodiscard]] Value global(std::string_view name) const;
    void setGlobal(std::string_view name, const Value& value);

    [[nodiscard]] const std::vector<ScriptError>& errors() const noexcept;
    void clearErrors() noexcept;
    [[nodiscard]] std::span<const Instance> instances() const noexcept;
    [[nodiscard]] const Instance* findInstance(std::string_view kind, std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const Instance*> instancesOf(std::string_view kind) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// The order loadAll runs `paths` in: lib/ first, then data/, then the rest, each alphabetically.
[[nodiscard]] std::vector<std::string> loadOrder(std::vector<std::string> paths);
} // namespace g7::script
