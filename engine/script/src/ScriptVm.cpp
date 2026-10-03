#include <g7/core/Log.hpp>
#include <g7/script/ScriptVm.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <map>
#include <stdexcept>

namespace g7::script
{
namespace
{
constexpr int kMaxDepth = 32;       // nested tables converted
constexpr int kHookInterval = 1000; // instructions between budget checks

/// sol2 appends a stack traceback to errors; messages keep only the error itself.
std::string withoutTraceback(std::string_view text)
{
    return std::string(text.substr(0, text.find("\nstack traceback:")));
}

/// "items/a.lua:3: message" -> file, line, message (other texts: message only).
ScriptError parseError(std::string_view text)
{
    ScriptError e;
    const usize lua = text.find(".lua:");
    if (lua != std::string_view::npos && text.find(' ') > lua)
    {
        const usize lineStart = lua + 5;
        const usize colon = text.find(':', lineStart);
        i32 line = 0;
        if (colon != std::string_view::npos &&
            std::from_chars(text.data() + lineStart, text.data() + colon, line).ec == std::errc{})
        {
            e.file = std::string(text.substr(0, lua + 4));
            e.line = line;
            e.message = std::string(text.substr(std::min(colon + 2, text.size())));
            return e;
        }
    }
    e.message = std::string(text);
    return e;
}

std::string_view typeName(FieldSpec::Type type)
{
    switch (type)
    {
    case FieldSpec::Type::Boolean:
        return "a boolean";
    case FieldSpec::Type::Integer:
        return "an integer";
    case FieldSpec::Type::Number:
        return "a number";
    case FieldSpec::Type::String:
        return "a string";
    case FieldSpec::Type::StringList:
        return "a list of strings";
    case FieldSpec::Type::Table:
        return "a table";
    case FieldSpec::Type::Function:
        return "a function";
    default:
        return "anything";
    }
}

bool hasType(const Value& v, FieldSpec::Type type)
{
    switch (type)
    {
    case FieldSpec::Type::Boolean:
        return v.isBool();
    case FieldSpec::Type::Integer:
        return v.isInteger();
    case FieldSpec::Type::Number:
        return v.isNumber();
    case FieldSpec::Type::String:
        return v.isString();
    case FieldSpec::Type::StringList:
        return v.isTable() && v.asTable()->fields.empty() &&
               std::all_of(v.asTable()->array.begin(), v.asTable()->array.end(),
                           [](const Value& e) { return e.isString(); });
    case FieldSpec::Type::Table:
        return v.isTable();
    case FieldSpec::Type::Function:
        return v.isFunction();
    default:
        return true;
    }
}
} // namespace

std::string ScriptError::text() const
{
    return file.empty() ? message
           : line > 0   ? std::format("{}:{}: {}", file, line, message)
                        : std::format("{}: {}", file, message);
}

std::vector<std::string> loadOrder(std::vector<std::string> paths)
{
    const auto rank = [](std::string_view p)
    {
        return p.starts_with("lib/") ? 0 : p.starts_with("data/") ? 1 : 2;
    };
    std::sort(paths.begin(), paths.end(), [&](const std::string& a, const std::string& b)
              { return rank(a) != rank(b) ? rank(a) < rank(b) : a < b; });
    return paths;
}

struct ScriptVm::Impl
{
    ScriptConfig config;
    usize memoryUsed = 0;
    u64 budget = 0; // instructions left in the current run
    sol::state lua;
    std::vector<InstanceKind> kinds;
    std::vector<Instance> instances;
    std::vector<ScriptError> errors;
    std::vector<sol::protected_function> functions;          // FunctionRef id - 1
    std::map<std::string, sol::object, std::less<>> modules; // require cache
    std::vector<Binding> bindings; // with the built-ins (docs only for print, require)
    struct Timer
    {
        u32 id = 0;
        f64 due = 0.0;
        f64 interval = 0.0; // 0: once
        FunctionRef function;
    };
    std::vector<Timer> timers;
    u32 nextTimer = 1;
    f64 clock = 0.0;
    std::map<std::string, std::vector<FunctionRef>, std::less<>> handlers;

    explicit Impl(ScriptConfig c) : config(std::move(c)), lua(&panic, &allocate, this) {}

    static int panic(lua_State* state)
    {
        G7_LOG_ERROR("script", "Lua panic: {}", lua_tostring(state, -1) ? lua_tostring(state, -1) : "?");
        std::abort(); // unprotected error: a bug in the engine side, never in scripts
    }

    static void* allocate(void* self, void* block, size_t oldSize, size_t newSize)
    {
        auto* impl = static_cast<Impl*>(self);
        const usize old = block != nullptr ? oldSize : 0;
        if (newSize == 0)
        {
            impl->memoryUsed -= old;
            std::free(block);
            return nullptr;
        }
        if (newSize > old && impl->memoryUsed + (newSize - old) > impl->config.memoryLimit)
        {
            return nullptr; // Lua raises "not enough memory"
        }
        void* grown = std::realloc(block, newSize);
        if (grown != nullptr)
        {
            impl->memoryUsed = impl->memoryUsed - old + newSize;
        }
        return grown;
    }

    static void hook(lua_State* state, lua_Debug*)
    {
        Impl* impl = *static_cast<Impl**>(lua_getextraspace(state));
        if (impl->budget <= static_cast<u64>(kHookInterval))
        {
            luaL_error(state, "instruction limit reached (endless loop?)");
        }
        impl->budget -= kHookInterval;
    }

    void startRun() { budget = config.instructionLimit; }

    void setupSandbox()
    {
        *static_cast<Impl**>(lua_getextraspace(lua.lua_state())) = this;
        lua_sethook(lua.lua_state(), &hook, LUA_MASKCOUNT, kHookInterval);
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8,
                           sol::lib::coroutine);
        // No files, no code from strings or bytecode, no garbage collector control.
        for (const char* name : {"dofile", "loadfile", "load", "loadstring", "collectgarbage"})
        {
            lua[name] = sol::lua_nil;
        }
        lua.set_function("print",
                         [this](sol::variadic_args args, sol::this_state state)
                         {
                             std::string line;
                             for (const sol::object& a : args)
                             {
                                 sol::state_view view(state);
                                 const std::string piece = view["tostring"](a);
                                 line += (line.empty() ? "" : "\t") + piece;
                             }
                             if (config.print)
                             {
                                 config.print(line);
                             }
                             else
                             {
                                 G7_LOG_INFO("script", "{}", line);
                             }
                         });
        lua.set_function("require", [this](std::string module, sol::this_state state)
                         { return require(module, state); });
        lua["Story"] = lua.create_table();
        addBuiltins();
    }

    /// "items/a.lua:3: " - where the running Lua code calls from (empty if unknown).
    static std::string where(lua_State* state)
    {
        lua_Debug ar{};
        if (lua_getstack(state, 1, &ar) != 0 && lua_getinfo(state, "Sl", &ar) != 0 && ar.currentline > 0)
        {
            const char* source = ar.source != nullptr && ar.source[0] == '@' ? ar.source + 1 : ar.short_src;
            return std::format("{}:{}: ", source, ar.currentline);
        }
        return {};
    }

    void bindLua(const Binding& b)
    {
        if (!b.function)
        {
            return; // documentation only (print, require, Story)
        }
        auto call = [this, name = b.name, function = b.function](sol::variadic_args va, sol::this_state state)
        {
            std::vector<Value> args;
            for (auto a : va)
            {
                args.push_back(toValue(sol::object(a), 0));
            }
            auto result = function(args);
            if (!result)
            {
                throw std::runtime_error(where(state) + name + ": " + result.error().message);
            }
            return toLua(result.value());
        };
        const usize dot = b.name.find('.');
        if (dot == std::string::npos)
        {
            lua.set_function(b.name, call);
            return;
        }
        const std::string table = b.name.substr(0, dot);
        if (lua[table].get_type() != sol::type::table)
        {
            lua[table] = lua.create_table();
        }
        sol::table t = lua[table];
        t.set_function(b.name.substr(dot + 1), call);
    }

    void addBuiltin(std::string name, std::string signature, std::string description,
                    std::function<Result<Value>(std::span<const Value>)> function = {})
    {
        Binding b{std::move(name), std::move(signature), std::move(description), "Grundlagen",
                  std::move(function)};
        bindLua(b);
        bindings.push_back(std::move(b));
    }

    void addBuiltins()
    {
        addBuiltin("print", "print(...)",
                   "Schreibt die Werte, mit Tabulatoren getrennt, ins Log bzw. in die Konsole.");
        addBuiltin(
            "require", "require(module: string) -> any",
            "Lädt `module` (Punkte trennen Ordner: `\"lib.util\"` = `lib/util.lua`) einmal und gibt sein "
            "Ergebnis zurück; nur unterhalb des Skript-Ordners.");
        addBuiltin(
            "Story", "Story",
            "Globale Tabelle der Story-Variablen (Zahlen, Strings, Wahrheitswerte, verschachtelte Tabellen); "
            "wird mit dem Spielstand gespeichert. Funktionen darin sind nicht erlaubt.");
        addBuiltin(
            "after", "after(seconds: number, fn: function) -> integer",
            "Ruft `fn` einmal nach `seconds` Sekunden Spielzeit auf; gibt die Nummer des Timers zurück.",
            [this](std::span<const Value> a) -> Result<Value> { return addTimer(a, false); });
        addBuiltin("every", "every(seconds: number, fn: function) -> integer",
                   "Ruft `fn` alle `seconds` Sekunden Spielzeit auf (`seconds` > 0); gibt die Nummer des "
                   "Timers zurück.",
                   [this](std::span<const Value> a) -> Result<Value> { return addTimer(a, true); });
        addBuiltin("cancel", "cancel(timer: integer) -> boolean",
                   "Hält einen Timer von `after`/`every` an; `false`, wenn es ihn nicht (mehr) gibt.",
                   [this](std::span<const Value> a) -> Result<Value>
                   {
                       const i64 id = a.empty() ? 0 : a[0].asInteger();
                       const auto before = timers.size();
                       std::erase_if(timers, [&](const Timer& t) { return static_cast<i64>(t.id) == id; });
                       return Value(timers.size() != before);
                   });
        addBuiltin(
            "on", "on(event: string, fn: function)",
            "Ruft `fn(...)` bei jedem Ereignis `event` auf (z. B. `\"world_loaded\"`); die Engine nennt ihre "
            "Ereignisse in dieser Datei.",
            [this](std::span<const Value> a) -> Result<Value>
            {
                if (a.size() < 2 || !a[0].isString() || !a[1].isFunction())
                {
                    return Error{"expects (event: string, fn: function)"};
                }
                handlers[std::string(a[0].asString())].push_back(a[1].asFunction());
                return Value();
            });
        addBuiltin("instance", "instance(kind: string, name: string) -> table | nil",
                   "Die Felder einer Instanz (`instance(\"Item\", \"it_apple\").value`), Funktionen darin "
                   "aufrufbar; "
                   "`nil`, wenn es sie nicht gibt.",
                   [this](std::span<const Value> a) -> Result<Value>
                   {
                       if (a.size() < 2 || !a[0].isString() || !a[1].isString())
                       {
                           return Error{"expects (kind: string, name: string)"};
                       }
                       const Instance* i = find(a[0].asString(), a[1].asString());
                       return i != nullptr ? i->fields : Value();
                   });
        addBuiltin("instances", "instances(kind: string) -> {string}",
                   "Die Namen aller Instanzen einer Art, alphabetisch (`instances(\"Npc\")`).",
                   [this](std::span<const Value> a) -> Result<Value>
                   {
                       if (a.empty() || !a[0].isString())
                       {
                           return Error{"expects (kind: string)"};
                       }
                       std::vector<std::string> names;
                       for (const Instance& i : instances)
                       {
                           if (i.kind == a[0].asString())
                           {
                               names.push_back(i.name);
                           }
                       }
                       std::sort(names.begin(), names.end());
                       return makeTable(std::vector<Value>(names.begin(), names.end()));
                   });
        addBuiltin("emit", "emit(event: string, ...) -> integer",
                   "Löst das Ereignis `event` mit den übrigen Argumenten aus; gibt die Zahl der aufgerufenen "
                   "Funktionen zurück.",
                   [this](std::span<const Value> a) -> Result<Value>
                   {
                       if (a.empty() || !a[0].isString())
                       {
                           return Error{"expects (event: string, ...)"};
                       }
                       return Value(static_cast<i64>(emitEvent(a[0].asString(), a.subspan(1))));
                   });
    }

    Result<Value> addTimer(std::span<const Value> a, bool repeat)
    {
        if (a.size() < 2 || !a[0].isNumber() || !a[1].isFunction() || a[0].asNumber() < 0.0 ||
            (repeat && a[0].asNumber() <= 0.0))
        {
            return Error{repeat ? "expects (seconds > 0, fn: function)"
                                : "expects (seconds >= 0, fn: function)"};
        }
        const u32 id = nextTimer++;
        timers.push_back({id, clock + a[0].asNumber(), repeat ? a[0].asNumber() : 0.0, a[1].asFunction()});
        return Value(static_cast<i64>(id));
    }

    Result<Value> callFunction(FunctionRef function, std::span<const Value> arguments)
    {
        if (!function.valid() || function.id > functions.size())
        {
            return Error{std::format("no function #{}", function.id)};
        }
        std::vector<sol::object> args;
        for (const Value& a : arguments)
        {
            args.push_back(toLua(a));
        }
        startRun();
        sol::protected_function_result result = functions[function.id - 1](sol::as_args(args));
        return finish(result);
    }

    usize emitEvent(std::string_view event, std::span<const Value> arguments)
    {
        const auto it = handlers.find(event);
        if (it == handlers.end())
        {
            return 0;
        }
        const std::vector<FunctionRef> list = it->second; // handlers may add handlers
        for (const FunctionRef f : list)
        {
            if (auto r = callFunction(f, arguments); !r)
            {
                G7_LOG_WARN("script", "event {}: {}", event, r.error().message);
            }
        }
        return list.size();
    }

    /// require "lib.util" -> lib/util.lua below the script root, run once.
    sol::object require(const std::string& module, sol::this_state)
    {
        if (const auto cached = modules.find(module); cached != modules.end())
        {
            return cached->second;
        }
        if (module.empty() || module.find("..") != std::string::npos ||
            module.find('/') != std::string::npos || module.find('\\') != std::string::npos)
        {
            throw std::runtime_error(
                std::format("require: '{}' is not a module name below the script folder", module));
        }
        std::string path = module;
        std::replace(path.begin(), path.end(), '.', '/');
        path += ".lua";
        auto code = config.readFile ? config.readFile(path) : Result<std::string>(Error{"no reader"});
        if (!code)
        {
            throw std::runtime_error("require: " + code.error().message);
        }
        sol::load_result chunk = lua.load(code.value(), "@" + path, sol::load_mode::text);
        if (!chunk.valid())
        {
            const sol::error error = chunk;
            throw std::runtime_error(error.what());
        }
        sol::protected_function run = chunk;
        sol::protected_function_result result = run();
        if (!result.valid())
        {
            const sol::error error = result;
            throw std::runtime_error(error.what());
        }
        sol::object value =
            result.return_count() > 0 ? result.get<sol::object>() : sol::make_object(lua, true);
        modules[module] = value;
        return value;
    }

    // --- values ---

    Value toValue(const sol::object& o, int depth)
    {
        switch (o.get_type())
        {
        case sol::type::boolean:
            return Value(o.as<bool>());
        case sol::type::number:
        {
            o.push(lua.lua_state());
            const bool integer = lua_isinteger(lua.lua_state(), -1) != 0;
            const Value v = integer ? Value(static_cast<i64>(lua_tointeger(lua.lua_state(), -1)))
                                    : Value(static_cast<f64>(lua_tonumber(lua.lua_state(), -1)));
            lua_pop(lua.lua_state(), 1);
            return v;
        }
        case sol::type::string:
            return Value(o.as<std::string>());
        case sol::type::function:
            functions.push_back(o.as<sol::protected_function>());
            return Value(FunctionRef{static_cast<u32>(functions.size())});
        case sol::type::table:
        {
            if (depth >= kMaxDepth)
            {
                return Value();
            }
            auto table = std::make_shared<Table>();
            const sol::table t = o.as<sol::table>();
            const usize length = t.size();
            for (usize i = 1; i <= length; ++i)
            {
                table->array.push_back(toValue(t[i], depth + 1));
            }
            t.for_each(
                [&](const sol::object& key, const sol::object& value)
                {
                    if (key.get_type() == sol::type::string)
                    {
                        table->fields[key.as<std::string>()] = toValue(value, depth + 1);
                    }
                });
            return Value(std::move(table));
        }
        default:
            return Value();
        }
    }

    sol::object toLua(const Value& v)
    {
        if (v.isBool())
        {
            return sol::make_object(lua, v.asBool());
        }
        if (v.isInteger())
        {
            return sol::make_object(lua, static_cast<lua_Integer>(v.asInteger()));
        }
        if (v.isNumber())
        {
            return sol::make_object(lua, v.asNumber());
        }
        if (v.isString())
        {
            return sol::make_object(lua, std::string(v.asString()));
        }
        if (v.isFunction() && v.asFunction().id <= functions.size())
        {
            return sol::make_object(lua, functions[v.asFunction().id - 1]);
        }
        if (const Table* t = v.asTable())
        {
            sol::table out =
                lua.create_table(static_cast<int>(t->array.size()), static_cast<int>(t->fields.size()));
            for (usize i = 0; i < t->array.size(); ++i)
            {
                out[i + 1] = toLua(t->array[i]);
            }
            for (const auto& [name, field] : t->fields)
            {
                out[name] = toLua(field);
            }
            return out;
        }
        return sol::make_object(lua, sol::lua_nil);
    }

    Result<Value> finish(sol::protected_function_result& result)
    {
        if (!result.valid())
        {
            const sol::error error = result;
            return Error{withoutTraceback(error.what())};
        }
        return result.return_count() > 0 ? toValue(result.get<sol::object>(), 0) : Value();
    }

    // --- instances ---

    void defineConstructor(const std::string& kind)
    {
        lua.set_function(
            kind,
            [this, kind](std::string name, sol::this_state state) -> sol::object
            {
                // Where `Kind "name"` stands: the calling Lua function.
                lua_Debug where{};
                std::string file;
                i32 line = 0;
                if (lua_getstack(state, 1, &where) != 0 && lua_getinfo(state, "Sl", &where) != 0)
                {
                    file = where.source != nullptr && where.source[0] == '@' ? where.source + 1 : "";
                    line = where.currentline;
                }
                return sol::make_object(
                    state,
                    [this, kind, name, file, line](sol::object fields)
                    {
                        if (fields.get_type() != sol::type::table)
                        {
                            errors.push_back(
                                {file, line, std::format("{} \"{}\" needs a table of fields", kind, name)});
                            return;
                        }
                        if (const auto* existing = find(kind, name))
                        {
                            errors.push_back({file, line,
                                              std::format("{} \"{}\" is already defined at {}:{}", kind, name,
                                                          existing->file, existing->line)});
                            return;
                        }
                        instances.push_back({kind, name, toValue(fields, 0), file, line});
                    });
            });
    }

    const Instance* find(std::string_view kind, std::string_view name) const
    {
        const auto it = std::find_if(instances.begin(), instances.end(),
                                     [&](const Instance& i) { return i.kind == kind && i.name == name; });
        return it == instances.end() ? nullptr : &*it;
    }

    void validate()
    {
        for (const Instance& instance : instances)
        {
            const auto kind = std::find_if(kinds.begin(), kinds.end(),
                                           [&](const InstanceKind& k) { return k.name == instance.kind; });
            if (kind == kinds.end())
            {
                continue;
            }
            const auto problem = [&](std::string message)
            {
                errors.push_back({instance.file, instance.line,
                                  std::format("{} \"{}\": {}", instance.kind, instance.name, message)});
            };
            const Table* fields = instance.fields.asTable();
            for (const FieldSpec& spec : kind->fields)
            {
                const Value& v = fields->field(spec.name);
                if (v.isNil())
                {
                    if (spec.required)
                    {
                        problem(std::format("field '{}' is missing", spec.name));
                    }
                    continue;
                }
                if (!hasType(v, spec.type))
                {
                    problem(std::format("field '{}' must be {}, not {}", spec.name, typeName(spec.type),
                                        v.typeName()));
                    continue;
                }
                if (v.isNumber() &&
                    ((spec.min && v.asNumber() < *spec.min) || (spec.max && v.asNumber() > *spec.max)))
                {
                    problem(std::format("field '{}' = {} is outside {} .. {}", spec.name, v.toString(),
                                        spec.min ? std::format("{}", *spec.min) : "",
                                        spec.max ? std::format("{}", *spec.max) : ""));
                }
                if (!spec.refKind.empty())
                {
                    std::vector<std::string_view> names;
                    if (v.isString())
                    {
                        names.push_back(v.asString());
                    }
                    if (const Table* list = v.asTable())
                    {
                        for (const Value& e : list->array)
                        {
                            names.push_back(e.asString());
                        }
                    }
                    for (const std::string_view n : names)
                    {
                        if (find(spec.refKind, n) == nullptr)
                        {
                            problem(std::format("field '{}': unknown {} \"{}\"", spec.name, spec.refKind, n));
                        }
                    }
                }
            }
            if (!kind->allowUnknownFields)
            {
                for (const auto& [name, value] : fields->fields)
                {
                    if (std::none_of(kind->fields.begin(), kind->fields.end(),
                                     [&](const FieldSpec& s) { return s.name == name; }))
                    {
                        problem(std::format("unknown field '{}'", name));
                    }
                }
            }
        }
    }
};

ScriptVm::ScriptVm() = default;
ScriptVm::~ScriptVm() = default;
ScriptVm::ScriptVm(ScriptVm&&) noexcept = default;
ScriptVm& ScriptVm::operator=(ScriptVm&&) noexcept = default;

Result<ScriptVm> ScriptVm::create(ScriptConfig config)
{
    ScriptVm vm;
    vm.m_impl = std::make_unique<Impl>(std::move(config));
    vm.m_impl->setupSandbox();
    return vm;
}

void ScriptVm::defineKind(InstanceKind kind)
{
    m_impl->defineConstructor(kind.name);
    m_impl->kinds.push_back(std::move(kind));
}

usize ScriptVm::loadAll()
{
    Impl& impl = *m_impl;
    usize ok = 0;
    const std::vector<std::string> files =
        loadOrder(impl.config.listFiles ? impl.config.listFiles() : std::vector<std::string>{});
    for (const std::string& path : files)
    {
        auto code = impl.config.readFile(path);
        if (!code)
        {
            impl.errors.push_back({path, 0, code.error().message});
            continue;
        }
        impl.startRun();
        sol::load_result chunk = impl.lua.load(code.value(), "@" + path, sol::load_mode::text);
        if (!chunk.valid())
        {
            const sol::error error = chunk;
            impl.errors.push_back(parseError(withoutTraceback(error.what())));
            continue;
        }
        sol::protected_function run = chunk;
        sol::protected_function_result result = run();
        if (!result.valid())
        {
            const sol::error error = result;
            impl.errors.push_back(parseError(withoutTraceback(error.what())));
            continue;
        }
        ++ok;
    }
    impl.validate();
    for (const ScriptError& e : impl.errors)
    {
        G7_LOG_WARN("script", "{}", e.text());
    }
    G7_LOG_INFO("script", "{} of {} scripts loaded, {} instances, {} problems", ok, files.size(),
                impl.instances.size(), impl.errors.size());
    return ok;
}

Result<Value> ScriptVm::runString(std::string_view code, std::string_view chunkName)
{
    Impl& impl = *m_impl;
    impl.startRun();
    // A console line is an expression first ("1 + 2" shows 3), else a statement.
    sol::load_result chunk = impl.lua.load("return " + std::string(code),
                                           std::string("=") + std::string(chunkName), sol::load_mode::text);
    if (!chunk.valid())
    {
        chunk = impl.lua.load(code, std::string("=") + std::string(chunkName), sol::load_mode::text);
    }
    if (!chunk.valid())
    {
        const sol::error error = chunk;
        return Error{withoutTraceback(error.what())};
    }
    sol::protected_function run = chunk;
    sol::protected_function_result result = run();
    return impl.finish(result);
}

Result<Value> ScriptVm::call(FunctionRef function, std::span<const Value> arguments)
{
    return m_impl->callFunction(function, arguments);
}

void ScriptVm::bind(Binding binding)
{
    m_impl->bindLua(binding);
    std::erase_if(m_impl->bindings, [&](const Binding& b) { return b.name == binding.name; });
    m_impl->bindings.push_back(std::move(binding));
}

std::vector<const Binding*> ScriptVm::bindings() const
{
    std::vector<const Binding*> out;
    for (const Binding& b : m_impl->bindings)
    {
        out.push_back(&b);
    }
    std::sort(out.begin(), out.end(),
              [](const Binding* a, const Binding* b)
              {
                  // Case-insensitive, so "Story" sorts among the lower-case names.
                  const auto lower = [](std::string_view s)
                  {
                      std::string out(s);
                      std::transform(out.begin(), out.end(), out.begin(),
                                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                      return out;
                  };
                  return a->group != b->group ? a->group < b->group : lower(a->name) < lower(b->name);
              });
    return out;
}

std::string ScriptVm::apiMarkdown() const
{
    std::string out =
        "# Skript-API (Lua)\n\n"
        "Erzeugt aus den Bindings der Engine (`gothar --script-api=docs/script-api.md`) – nicht von Hand "
        "bearbeiten.\nSprache, Sandbox, Lade-Reihenfolge und Instanzen: `docs/modules/script.md`.\n";
    std::string group;
    for (const Binding* b : bindings())
    {
        if (b->group != group)
        {
            group = b->group;
            out += std::format("\n## {}\n", group);
        }
        out += std::format("\n### `{}`\n{}\n", b->signature, b->description);
    }
    return out;
}

Value ScriptVm::story() const
{
    return m_impl->toValue(m_impl->lua["Story"], 0);
}

namespace
{
std::optional<std::string> firstFunction(const Value& v, const std::string& path)
{
    if (v.isFunction())
    {
        return path;
    }
    if (const Table* t = v.asTable())
    {
        for (usize i = 0; i < t->array.size(); ++i)
        {
            if (auto found = firstFunction(t->array[i], std::format("{}[{}]", path, i + 1)))
            {
                return found;
            }
        }
        for (const auto& [name, field] : t->fields)
        {
            if (auto found = firstFunction(field, path + "." + name))
            {
                return found;
            }
        }
    }
    return std::nullopt;
}
} // namespace

Result<Value> ScriptVm::storyForSave() const
{
    Value s = story();
    if (auto f = firstFunction(s, "Story"))
    {
        return Error{std::format("{} is a function: story variables hold data only", *f)};
    }
    return s;
}

Result<void> ScriptVm::setStory(const Value& story)
{
    if (!story.isTable())
    {
        return Error{"Story must be a table"};
    }
    m_impl->lua["Story"] = m_impl->toLua(story);
    return {};
}

usize ScriptVm::tick(f64 seconds)
{
    Impl& impl = *m_impl;
    impl.clock += seconds;
    usize calls = 0;
    // Due timers in order; a timer started by a callback waits for the next tick.
    std::vector<Impl::Timer> due;
    for (const Impl::Timer& t : impl.timers)
    {
        if (t.due <= impl.clock)
        {
            due.push_back(t);
        }
    }
    std::sort(due.begin(), due.end(), [](const Impl::Timer& a, const Impl::Timer& b)
              { return a.due != b.due ? a.due < b.due : a.id < b.id; });
    for (const Impl::Timer& t : due)
    {
        const auto still = std::find_if(impl.timers.begin(), impl.timers.end(),
                                        [&](const Impl::Timer& x) { return x.id == t.id; });
        if (still == impl.timers.end())
        {
            continue; // cancelled by an earlier callback
        }
        if (still->interval > 0.0)
        {
            // Missed periods are not caught up: at most one call per tick.
            still->due += still->interval;
            if (still->due <= impl.clock)
            {
                still->due = impl.clock + still->interval;
            }
        }
        else
        {
            impl.timers.erase(still);
        }
        ++calls;
        if (auto r = impl.callFunction(t.function, {}); !r)
        {
            G7_LOG_WARN("script", "timer {}: {} (dropped)", t.id, r.error().message);
            std::erase_if(impl.timers, [&](const Impl::Timer& x) { return x.id == t.id; });
        }
    }
    return calls;
}

f64 ScriptVm::time() const noexcept
{
    return m_impl->clock;
}

usize ScriptVm::emit(std::string_view event, std::span<const Value> arguments)
{
    return m_impl->emitEvent(event, arguments);
}

Result<Value> ScriptVm::callGlobal(std::string_view name, std::span<const Value> arguments)
{
    Impl& impl = *m_impl;
    const sol::object f = impl.lua[std::string(name)];
    if (f.get_type() != sol::type::function)
    {
        return Error{std::format("no function '{}'", name)};
    }
    std::vector<sol::object> args;
    for (const Value& a : arguments)
    {
        args.push_back(impl.toLua(a));
    }
    impl.startRun();
    sol::protected_function_result result = f.as<sol::protected_function>()(sol::as_args(args));
    return impl.finish(result);
}

Value ScriptVm::global(std::string_view name) const
{
    return m_impl->toValue(m_impl->lua[std::string(name)], 0);
}

void ScriptVm::setGlobal(std::string_view name, const Value& value)
{
    m_impl->lua[std::string(name)] = m_impl->toLua(value);
}

const std::vector<ScriptError>& ScriptVm::errors() const noexcept
{
    return m_impl->errors;
}

void ScriptVm::clearErrors() noexcept
{
    m_impl->errors.clear();
}

std::span<const Instance> ScriptVm::instances() const noexcept
{
    return m_impl->instances;
}

const Instance* ScriptVm::findInstance(std::string_view kind, std::string_view name) const noexcept
{
    return m_impl->find(kind, name);
}

std::vector<const Instance*> ScriptVm::instancesOf(std::string_view kind) const
{
    std::vector<const Instance*> out;
    for (const Instance& i : m_impl->instances)
    {
        if (i.kind == kind)
        {
            out.push_back(&i);
        }
    }
    return out;
}
} // namespace g7::script
