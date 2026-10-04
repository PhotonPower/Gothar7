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
    /// Allowed values of a String field (empty: any).
    std::vector<std::string> oneOf;
};

/// An instance constructor in Lua, `<name> "<instance>" { fields }`, with the schema its tables must follow.
struct InstanceKind
{
    std::string name; ///< "Item", "Npc", "Info", "Quest", "Routine"
    std::vector<FieldSpec> fields;
    bool allowUnknownFields = false;
};

/// A function the engine gives to scripts. The module that owns the feature registers it (gameplay: `say`,
/// ai: `goto_waypoint` ...), so script needs no dependency upwards; docs/script-api.md is generated from
/// these.
struct Binding
{
    std::string name;        ///< "insert", or "Log.create" for a function in a global table
    std::string signature;   ///< "insert(instance: string, count?: integer) -> boolean"
    std::string description; ///< German, for docs/script-api.md
    std::string group;       ///< chapter of docs/script-api.md ("Welt", "Story" ...)
    /// Errors become Lua errors at the calling line ("items/a.lua:3: insert: unknown item ...").
    std::function<Result<Value>(std::span<const Value> arguments)> function;
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

    /// Makes `binding.function` callable from Lua under `binding.name` (also after loadAll).
    void bind(Binding binding);
    /// Every binding and built-in (print, require, after, every, cancel, on, emit, Story), for the docs.
    [[nodiscard]] std::vector<const Binding*> bindings() const;
    /// docs/script-api.md: the bindings by group, sorted, with signature and description.
    [[nodiscard]] std::string apiMarkdown() const;

    /// The global table `Story`: the story variables (numbers, strings, booleans, nested tables).
    [[nodiscard]] Value story() const;
    /// For the save game: `Story` without functions - an error names the first function found.
    [[nodiscard]] Result<Value> storyForSave() const;
    /// Back from a save game (a table).
    [[nodiscard]] Result<void> setStory(const Value& story);

    /// Advances the script clock (game time, seconds) and runs the timers that are due (`after`, `every`).
    /// A failing timer is logged and dropped. Returns the number of timer calls.
    usize tick(f64 seconds);
    [[nodiscard]] f64 time() const noexcept;
    /// Calls every handler registered with `on(event, fn)` (and Lua's emit). Failing handlers are logged.
    usize emit(std::string_view event, std::span<const Value> arguments = {});

    [[nodiscard]] const std::vector<ScriptError>& errors() const noexcept;
    void clearErrors() noexcept;
    [[nodiscard]] std::span<const Instance> instances() const noexcept;
    /// `name` may carry a "#n" suffix (several NPCs of one instance, "mon_wolf#2"): the instance is the same.
    [[nodiscard]] const Instance* findInstance(std::string_view kind, std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const Instance*> instancesOf(std::string_view kind) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// The order loadAll runs `paths` in: lib/ first, then data/, then the rest, each alphabetically.
[[nodiscard]] std::vector<std::string> loadOrder(std::vector<std::string> paths);
} // namespace g7::script
