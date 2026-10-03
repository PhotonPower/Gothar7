// Script VM (M7 part A, ADR 0006): values, load order, sandbox, errors with file and line, limits,
// declarative instances and their schema checks, console code, calls. Scripts come from an in-memory
// "folder".

#include <g7/script/ScriptVm.hpp>
#include <g7/script/Value.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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
