// Script VM (M7 part A, ADR 0006): values, load order, sandbox, errors with file and line, limits,
// declarative instances and their schema checks, console code, calls. Scripts come from an in-memory
// "folder".

#include <g7/script/ScriptVm.hpp>
#include <g7/script/Value.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <map>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::script;

namespace
{
struct Files
{
    std::map<std::string, std::string> files;
    std::vector<std::string> printed;

    ScriptConfig config()
    {
        ScriptConfig c;
        c.readFile = [this](std::string_view path) -> Result<std::string>
        {
            const auto it = files.find(std::string(path));
            if (it == files.end())
            {
                return Error{std::string(path) + ": no such script"};
            }
            return it->second;
        };
        c.listFiles = [this]
        {
            std::vector<std::string> out;
            for (const auto& [path, code] : files)
            {
                out.push_back(path);
            }
            return out;
        };
        c.print = [this](std::string_view line) { printed.emplace_back(line); };
        return c;
    }
};

ScriptVm makeVm(Files& files)
{
    auto vm = ScriptVm::create(files.config());
    REQUIRE(vm.ok());
    return std::move(vm).value();
}

InstanceKind itemKind()
{
    InstanceKind item;
    item.name = "Item";
    item.fields = {{"name", FieldSpec::Type::String, true},
                   {"value", FieldSpec::Type::Integer, false, 0.0, 100000.0},
                   {"weight", FieldSpec::Type::Number},
                   {"tags", FieldSpec::Type::StringList}};
    return item;
}

InstanceKind npcKind()
{
    InstanceKind npc;
    npc.name = "Npc";
    npc.fields = {{"name", FieldSpec::Type::String, true},
                  {"level", FieldSpec::Type::Integer, true, 1.0, 100.0},
                  {"equipment", FieldSpec::Type::StringList, false, std::nullopt, std::nullopt, "Item"},
                  {"weapon", FieldSpec::Type::String, false, std::nullopt, std::nullopt, "Item"},
                  {"on_talk", FieldSpec::Type::Function}};
    return npc;
}
} // namespace

TEST_CASE("Script values: types, fields, text, equality")
{
    CHECK(Value().isNil());
    CHECK(Value(3).isInteger());
    CHECK(Value(3).isNumber());
    CHECK(Value(2.5).asNumber() == doctest::Approx(2.5));
    CHECK(Value("x").asString() == "x");
    CHECK(Value(3) == Value(3.0));
    CHECK(Value(3).typeName() == "integer");
    const Value t = makeTable({1, 2}, {{"name", "Sword"}, {"value", 40}});
    CHECK(t["name"].asString() == "Sword");
    CHECK(t["missing"].isNil());
    CHECK(Value(5)["x"].isNil());
    CHECK(t.toString() == "{1, 2, name = \"Sword\", value = 40}");
    CHECK(t == makeTable({1, 2}, {{"value", 40}, {"name", "Sword"}}));
}

TEST_CASE("Script load order: lib, data, then the rest, alphabetically")
{
    const auto order =
        loadOrder({"npcs/b.lua", "data/x.lua", "lib/z.lua", "items/a.lua", "lib/a.lua", "startup.lua"});
    CHECK(order == std::vector<std::string>{"lib/a.lua", "lib/z.lua", "data/x.lua", "items/a.lua",
                                            "npcs/b.lua", "startup.lua"});

    Files files;
    files.files["lib/base.lua"] = "trace = { 'lib' }";
    files.files["data/guilds.lua"] = "table.insert(trace, 'data')";
    files.files["items/a.lua"] = "table.insert(trace, 'items')";
    ScriptVm vm = makeVm(files);
    CHECK(vm.loadAll() == 3);
    CHECK(vm.global("trace") == makeTable({"lib", "data", "items"}));
}

TEST_CASE("Script sandbox: no files, os, debug or code from strings; print goes to the engine")
{
    Files files;
    ScriptVm vm = makeVm(files);
    for (const char* code :
         {"io.open('x')", "os.execute('x')", "debug.getinfo(1)", "load('return 1')", "dofile('x.lua')",
          "loadfile('x.lua')", "collectgarbage()", "require('../secret')", "require('lib/x')"})
    {
        CAPTURE(code);
        CHECK_FALSE(vm.runString(code).ok());
    }
    CHECK(vm.runString("string.upper('a') .. math.floor(2.7) .. #table.pack(1, 2)").value().asString() ==
          "A22");
    CHECK(vm.runString("print('hello', 42)").ok());
    REQUIRE(files.printed.size() == 1);
    CHECK(files.printed[0] == "hello\t42");
}

TEST_CASE("Script errors: file and line, other files go on; instruction and memory limits")
{
    Files files;
    files.files["items/broken.lua"] = "x = 1\nthis is not lua\n";
    files.files["items/crash.lua"] = "local t = nil\n\nprint(t.field)\n";
    files.files["items/good.lua"] = "good = true";
    files.files["items/loop.lua"] = "while true do end";
    ScriptVm vm = makeVm(files);
    CHECK(vm.loadAll() == 1);
    CHECK(vm.global("good").asBool());
    REQUIRE(vm.errors().size() == 3);
    CHECK(vm.errors()[0].file == "items/broken.lua");
    CHECK(vm.errors()[0].line == 2);
    CHECK(vm.errors()[1].file == "items/crash.lua");
    CHECK(vm.errors()[1].line == 3);
    CHECK(vm.errors()[1].text().starts_with("items/crash.lua:3: "));
    CHECK(vm.errors()[2].message.find("instruction limit") != std::string::npos);

    ScriptConfig small = files.config();
    small.memoryLimit = usize{4} << 20;
    ScriptVm limited = ScriptVm::create(small).value();
    CHECK_FALSE(limited.runString("local t = {} for i = 1, 1e7 do t[i] = tostring(i) end").ok());
    CHECK(limited.runString("1 + 1").value().asInteger() == 2); // still usable afterwards
}

TEST_CASE("Script instances: declared in Lua, checked against their kind")
{
    Files files;
    files.files["items/weapons.lua"] = R"(
Item "it_sword_old" {
    name = "Altes Schwert", value = 40, weight = 2.5, tags = { "melee", "1h" },
}
Item "it_apple" { name = "Apfel", value = 2 }
)";
    files.files["npcs/guard.lua"] = R"(
Npc "npc_guard" {
    name = "Torwache", level = 12, equipment = { "it_sword_old", "it_apple" }, weapon = "it_sword_old",
    on_talk = function(self, other) return self .. " greets " .. other end,
}
)";
    files.files["npcs/wrong.lua"] = R"(
Npc "npc_wrong" {
    name = 7, level = 300, equipment = { "it_sword_new" }, mood = "grim",
}
Npc "npc_empty" { level = 3 }
Item "it_apple" { name = "Zweiter Apfel" }
Item "it_nothing" "oops"
)";
    ScriptVm vm = makeVm(files);
    vm.defineKind(itemKind());
    vm.defineKind(npcKind());
    vm.loadAll();

    const Instance* sword = vm.findInstance("Item", "it_sword_old");
    REQUIRE(sword != nullptr);
    CHECK(sword->fields["value"].asInteger() == 40);
    CHECK(sword->fields["tags"] == makeTable({"melee", "1h"}));
    CHECK(sword->file == "items/weapons.lua");
    CHECK(sword->line == 2);
    CHECK(vm.instancesOf("Item").size() == 2);
    CHECK(vm.findInstance("Npc", "npc_guard") != nullptr);

    // A function in an instance is kept and can be called.
    const FunctionRef talk = vm.findInstance("Npc", "npc_guard")->fields["on_talk"].asFunction();
    REQUIRE(talk.valid());
    const std::vector<Value> args = {"guard", "hero"};
    CHECK(vm.call(talk, args).value().asString() == "guard greets hero");
    // ... and from Lua: instance() and instances().
    CHECK(vm.runString("instance('Item', 'it_apple').value").value().asInteger() == 2);
    CHECK(vm.runString("instance('Npc', 'npc_guard').on_talk('a', 'b')").value().asString() == "a greets b");
    CHECK(vm.runString("instance('Item', 'it_unknown')").value().isNil());
    CHECK(vm.runString("instances('Item')").value() == makeTable({"it_apple", "it_sword_old"}));

    // The problems, each with file, line and what is wrong.
    std::vector<std::string> texts;
    for (const ScriptError& e : vm.errors())
    {
        texts.push_back(e.text());
    }
    const auto has = [&](std::string_view part)
    {
        return std::any_of(texts.begin(), texts.end(),
                           [&](const std::string& t) { return t.find(part) != std::string::npos; });
    };
    std::string all;
    for (const std::string& t : texts)
    {
        all += t + "\n";
    }
    CAPTURE(all);
    CHECK(has("npcs/wrong.lua:6: Item \"it_apple\" is already defined at items/weapons.lua:5"));
    CHECK(has("npcs/wrong.lua:7: Item \"it_nothing\" needs a table of fields"));
    CHECK(has("Npc \"npc_wrong\": field 'name' must be a string, not integer"));
    CHECK(has("Npc \"npc_wrong\": field 'level' = 300 is outside 1 .. 100"));
    CHECK(has("Npc \"npc_wrong\": field 'equipment': unknown Item \"it_sword_new\""));
    CHECK(has("Npc \"npc_wrong\": unknown field 'mood'"));
    CHECK(has("npcs/wrong.lua:5: Npc \"npc_empty\": field 'name' is missing"));
    CHECK(texts.size() == 7);
}

TEST_CASE("Script console and calls: expressions, statements, globals, modules")
{
    Files files;
    files.files["lib/util.lua"] = "loads = (loads or 0) + 1\nreturn { twice = function(x) return 2 * x end }";
    files.files["startup.lua"] =
        "local util = require 'lib.util'\nfunction double(x) return util.twice(x) end";
    ScriptVm vm = makeVm(files);
    vm.loadAll();
    CHECK(vm.errors().empty());
    CHECK(vm.runString("1 + 2").value().asInteger() == 3);
    CHECK(vm.runString("x = 5").value().isNil());
    CHECK(vm.global("x").asInteger() == 5);
    CHECK(vm.runString("double(x)").value().asInteger() == 10);
    const auto failed = vm.runString("nothing()");
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().message.find("nothing") != std::string::npos);

    const std::vector<Value> args = {21};
    CHECK(vm.callGlobal("double", args).value().asInteger() == 42);
    CHECK_FALSE(vm.callGlobal("missing").ok());

    // Nested tables there and back.
    const Value story =
        makeTable({}, {{"met_guard", true}, {"gold", 120}, {"quests", makeTable({"q1", "q2"})}});
    vm.setGlobal("Story", story);
    CHECK(vm.global("Story") == story);
    CHECK(vm.runString("Story.quests[2]").value().asString() == "q2");
}

TEST_CASE("Script bindings: engine functions in Lua, errors at the calling line, generated docs")
{
    Files files;
    files.files["items/use.lua"] =
        "total = add(2, 3)\nLog.create('topic_a', 'first entry')\ninsert('it_dragon')\n";
    ScriptVm vm = makeVm(files);
    std::vector<std::string> log;
    vm.bind({"add", "add(a: number, b: number) -> number", "Zählt zusammen.", "Test",
             [](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() != 2 || !a[0].isNumber() || !a[1].isNumber())
                 {
                     return Error{"expects two numbers"};
                 }
                 return Value(a[0].asNumber() + a[1].asNumber());
             }});
    vm.bind({"Log.create", "Log.create(topic: string, text: string)", "Neues Tagebuch-Thema.", "Tagebuch",
             [&](std::span<const Value> a) -> Result<Value>
             {
                 log.push_back(std::string(a[0].asString()) + ": " + std::string(a[1].asString()));
                 return Value();
             }});
    vm.bind({"insert", "insert(instance: string)", "Setzt eine Instanz in die Welt.", "Welt",
             [](std::span<const Value> a) -> Result<Value>
             { return Error{std::format("unknown instance \"{}\"", a.empty() ? "" : a[0].asString())}; }});
    vm.loadAll();
    CHECK(vm.global("total").asNumber() == doctest::Approx(5.0));
    CHECK(log == std::vector<std::string>{"topic_a: first entry"});
    REQUIRE(vm.errors().size() == 1);
    CHECK(vm.errors()[0].text() == "items/use.lua:3: insert: unknown instance \"it_dragon\"");
    CHECK_FALSE(vm.runString("add('x')").ok());

    // Docs: groups sorted, built-ins under "Grundlagen", each with signature and description.
    std::vector<std::string> names;
    for (const Binding* b : vm.bindings())
    {
        names.push_back(b->name);
    }
    CHECK(names == std::vector<std::string>{"after", "cancel", "emit", "every", "instance", "instances", "on",
                                            "print", "require", "Story", "Log.create", "add", "insert"});
    const std::string md = vm.apiMarkdown();
    CHECK(md.starts_with("# Skript-API (Lua)"));
    CHECK(md.find("## Grundlagen") < md.find("## Tagebuch"));
    CHECK(md.find("### `insert(instance: string)`\nSetzt eine Instanz in die Welt.") != std::string::npos);
}

TEST_CASE("Script story, timers and events")
{
    Files files;
    files.files["startup.lua"] = R"(
Story.met_guard = false
Story.gold = 10
calls = { once = 0, repeated = 0, events = 0 }
after(1.0, function() calls.once = calls.once + 1 end)
repeat_id = every(0.5, function() calls.repeated = calls.repeated + 1 end)
after(0.5, function() error('broken timer') end)
on('world_loaded', function(name) calls.events = calls.events + 1; calls.world = name end)
on('world_loaded', function() error('broken handler') end)
on('world_loaded', function() calls.events = calls.events + 1 end)
)";
    ScriptVm vm = makeVm(files);
    vm.loadAll();
    REQUIRE(vm.errors().empty());

    // Story: data only, saved and restored.
    CHECK(vm.story()["gold"].asInteger() == 10);
    REQUIRE(vm.storyForSave().ok());
    CHECK(vm.runString("Story.callback = function() end").ok());
    const auto bad = vm.storyForSave();
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().message.find("Story.callback") != std::string::npos);
    CHECK(vm.setStory(makeTable({}, {{"met_guard", true}, {"gold", 99}})).ok());
    CHECK(vm.runString("Story.gold").value().asInteger() == 99);
    CHECK(vm.storyForSave().ok());
    CHECK_FALSE(vm.setStory(Value(3)).ok());

    // Timers: game time; the broken one is dropped, repeating ones do not catch up missed periods.
    CHECK(vm.tick(0.5) == 2); // repeated + broken
    CHECK(vm.tick(0.5) == 2); // once + repeated
    CHECK(vm.runString("calls.once").value().asInteger() == 1);
    CHECK(vm.runString("calls.repeated").value().asInteger() == 2);
    CHECK(vm.tick(10.0) == 1); // one call, not twenty
    CHECK(vm.runString("cancel(repeat_id)").value().asBool());
    CHECK_FALSE(vm.runString("cancel(repeat_id)").value().asBool());
    CHECK(vm.tick(1.0) == 0);
    CHECK(vm.time() == doctest::Approx(12.0));
    CHECK_FALSE(vm.runString("every(0, function() end)").ok());

    // Events from the engine and from Lua; a failing handler does not stop the others.
    const std::vector<Value> args = {"camp"};
    CHECK(vm.emit("world_loaded", args) == 3);
    CHECK(vm.runString("calls.events").value().asInteger() == 2);
    CHECK(vm.runString("calls.world").value().asString() == "camp");
    CHECK(vm.runString("emit('world_loaded', 'cave')").value().asInteger() == 3);
    CHECK(vm.runString("calls.world").value().asString() == "cave");
    CHECK(vm.emit("nobody_listens") == 0);
}

TEST_CASE("Script functions handed to the engine while a handler or timer runs")
{
    // welt 2026-10-04: a timer created in an event handler crashed when it fired. The handler being called
    // was an element of the function table, which grew (and moved) while it ran.
    Files files;
    files.files["startup.lua"] = R"(
fired = 0
on('world_loaded', function(world)
    for i = 1, 100 do -- enough to make the table grow
        after(1.0, function() fired = fired + 1 end)
    end
end)
every(0.5, function()
    for i = 1, 100 do
        after(0.25, function() fired = fired + 1000 end)
    end
end)
)";
    ScriptVm vm = makeVm(files);
    vm.loadAll();
    REQUIRE(vm.errors().empty());
    const std::vector<Value> args = {"camp"};
    CHECK(vm.emit("world_loaded", args) == 1);
    vm.tick(0.5);  // the repeating timer adds 100 one-shots
    vm.tick(0.25); // ... which fire
    vm.tick(0.25); // the handler's 100 fire; the repeating timer adds another 100
    CHECK(vm.runString("fired").value().asInteger() == 100 * 1000 + 100);
}
