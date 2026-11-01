#include <g7/save/GameState.hpp>

#include <nlohmann/json.hpp>

#include <format>

namespace g7::save
{
namespace
{
using Json = nlohmann::json; // keys sorted: stable text

// --- writing ---

Json vec3(const Vec3& v)
{
    return Json::array({v.x, v.y, v.z});
}

Json valueJson(const script::Value& v)
{
    const auto& s = v.storage();
    if (std::holds_alternative<bool>(s))
    {
        return std::get<bool>(s);
    }
    if (std::holds_alternative<i64>(s))
    {
        return std::get<i64>(s);
    }
    if (std::holds_alternative<f64>(s))
    {
        return Json{{"f", std::get<f64>(s)}}; // keeps 2.0 a number, not an integer
    }
    if (std::holds_alternative<std::string>(s))
    {
        return std::get<std::string>(s);
    }
    if (const script::Table* t = v.asTable())
    {
        Json array = Json::array();
        for (const script::Value& e : t->array)
        {
            array.push_back(valueJson(e));
        }
        Json fields = Json::object();
        for (const auto& [name, e] : t->fields)
        {
            fields[name] = valueJson(e);
        }
        return Json{{"array", std::move(array)}, {"fields", std::move(fields)}};
    }
    return nullptr; // nil (functions are refused before)
}

Json characterJson(const gameplay::CharacterState& c)
{
    Json equipped = Json::object();
    for (usize i = 0; i < c.equipped.size(); ++i)
    {
        if (!c.equipped[i].empty())
        {
            equipped[std::string(gameplay::slotName(static_cast<gameplay::EquipSlot>(i)))] = c.equipped[i];
        }
    }
    return Json{{"instance", c.instance},
                {"name", c.name},
                {"guild", c.guild},
                {"attributes", c.attributes},
                {"talents", c.talents},
                {"protection", c.protection},
                {"level", c.level},
                {"experience", c.experience},
                {"learnPoints", c.learnPoints},
                {"items", c.items},
                {"equipped", std::move(equipped)}};
}

Json vobJson(const world::WorldFileVob& vob)
{
    return Json::parse(world::writeVobJson(vob));
}

std::string_view lifeName(Life life)
{
    switch (life)
    {
    case Life::Down:
        return "down";
    case Life::Dead:
        return "dead";
    case Life::Vanished:
        return "vanished";
    case Life::Alive:
        break;
    }
    return "alive";
}

// --- reading ---

struct Reader
{
    std::string error;
    bool failed(std::string where, std::string what)
    {
        if (error.empty())
        {
            error = std::format("{}: {}", where, what);
        }
        return false;
    }
};

const Json* field(Reader& r, const Json& object, const char* key, const std::string& where)
{
    if (!object.is_object() || !object.contains(key))
    {
        r.failed(where, std::format("missing '{}'", key));
        return nullptr;
    }
    return &object[key];
}

template <typename T>
bool read(Reader& r, const Json& object, const char* key, const std::string& where, T& out)
{
    const Json* j = field(r, object, key, where);
    if (j == nullptr)
    {
        return false;
    }
    try
    {
        out = j->get<T>();
    }
    catch (const Json::exception&)
    {
        return r.failed(std::format("{}.{}", where, key), "wrong type");
    }
    return true;
}

bool readVec3(Reader& r, const Json& object, const char* key, const std::string& where, Vec3& out)
{
    std::vector<f32> v;
    if (!read(r, object, key, where, v))
    {
        return false;
    }
    if (v.size() != 3)
    {
        return r.failed(std::format("{}.{}", where, key), "needs three numbers");
    }
    out = Vec3(v[0], v[1], v[2]);
    return true;
}

bool readValue(Reader& r, const Json& j, const std::string& where, script::Value& out)
{
    if (j.is_null())
    {
        out = script::Value();
    }
    else if (j.is_boolean())
    {
        out = j.get<bool>();
    }
    else if (j.is_number_integer())
    {
        out = j.get<i64>();
    }
    else if (j.is_string())
    {
        out = j.get<std::string>();
    }
    else if (j.is_object() && j.contains("f") && j.size() == 1 && j["f"].is_number())
    {
        out = j["f"].get<f64>();
    }
    else if (j.is_object() && j.contains("array") && j.contains("fields") && j["array"].is_array() &&
             j["fields"].is_object())
    {
        std::vector<script::Value> array;
        for (usize i = 0; i < j["array"].size(); ++i)
        {
            script::Value e;
            if (!readValue(r, j["array"][i], std::format("{}[{}]", where, i + 1), e))
            {
                return false;
            }
            array.push_back(std::move(e));
        }
        std::map<std::string, script::Value, std::less<>> fields;
        for (const auto& [name, value] : j["fields"].items())
        {
            script::Value e;
            if (!readValue(r, value, std::format("{}.{}", where, name), e))
            {
                return false;
            }
            fields.emplace(name, std::move(e));
        }
        out = script::makeTable(std::move(array), std::move(fields));
    }
    else
    {
        return r.failed(where, "not a script value");
    }
    return true;
}

bool readCharacter(Reader& r, const Json& j, const std::string& where, gameplay::CharacterState& c)
{
    if (!read(r, j, "instance", where, c.instance) || !read(r, j, "name", where, c.name) ||
        !read(r, j, "guild", where, c.guild) || !read(r, j, "attributes", where, c.attributes) ||
        !read(r, j, "talents", where, c.talents) || !read(r, j, "protection", where, c.protection) ||
        !read(r, j, "level", where, c.level) || !read(r, j, "experience", where, c.experience) ||
        !read(r, j, "learnPoints", where, c.learnPoints) || !read(r, j, "items", where, c.items))
    {
        return false;
    }
    const Json* equipped = field(r, j, "equipped", where);
    if (equipped == nullptr || !equipped->is_object())
    {
        return r.failed(where + ".equipped", "must be an object");
    }
    for (const auto& [slot, item] : equipped->items())
    {
        const auto s = gameplay::slotFromName(slot);
        if (!s || !item.is_string())
        {
            return r.failed(std::format("{}.equipped.{}", where, slot), "unknown slot or not an item");
        }
        c.equipped[static_cast<usize>(*s)] = item.get<std::string>();
    }
    return true;
}

bool readVobs(Reader& r, const Json& j, const char* key, const std::string& where,
              std::vector<world::WorldFileVob>& out)
{
    const Json* list = field(r, j, key, where);
    if (list == nullptr || !list->is_array())
    {
        return r.failed(std::format("{}.{}", where, key), "must be an array");
    }
    for (usize i = 0; i < list->size(); ++i)
    {
        const std::string at = std::format("{}.{}[{}]", where, key, i);
        auto vob = world::parseVobJson((*list)[i].dump(), at);
        if (!vob)
        {
            return r.failed(at, vob.error().message);
        }
        out.push_back(std::move(vob).value());
    }
    return true;
}

bool readNpc(Reader& r, const Json& j, const std::string& where, NpcState& n)
{
    std::string life;
    if (!read(r, j, "name", where, n.name) || !read(r, j, "instance", where, n.instance) ||
        !readVec3(r, j, "position", where, n.position) || !read(r, j, "yaw", where, n.yaw) ||
        !read(r, j, "routine", where, n.routine) || !read(r, j, "life", where, life) ||
        !read(r, j, "downSeconds", where, n.downSeconds) ||
        !read(r, j, "pickpocketed", where, n.pickpocketed) || !read(r, j, "summoned", where, n.summoned) ||
        !read(r, j, "summonSeconds", where, n.summonSeconds))
    {
        return false;
    }
    if (life == "alive" || life == "down" || life == "dead" || life == "vanished")
    {
        n.life = life == "down"       ? Life::Down
                 : life == "dead"     ? Life::Dead
                 : life == "vanished" ? Life::Vanished
                                      : Life::Alive;
    }
    else
    {
        return r.failed(where + ".life", "alive, down, dead or vanished");
    }
    if (j.contains("character"))
    {
        gameplay::CharacterState c;
        if (!readCharacter(r, j["character"], where + ".character", c))
        {
            return false;
        }
        n.character = std::move(c);
    }
    return true;
}
} // namespace

std::string toJson(const GameState& state)
{
    Json root = Json::object();
    root["version"] = state.version;
    root["clock"] = state.clock;
    root["secondsPerMinute"] = state.secondsPerMinute;
    root["scriptClock"] = state.scriptClock;
    root["nextTimerId"] = state.nextTimerId;
    Json timers = Json::array();
    for (const script::ScriptVm::NamedTimer& t : state.timers)
    {
        Json args = Json::array();
        for (const script::Value& a : t.arguments)
        {
            args.push_back(valueJson(a));
        }
        timers.push_back(Json{{"id", t.id},
                              {"due", t.due},
                              {"interval", t.interval},
                              {"function", t.function},
                              {"arguments", std::move(args)}});
    }
    root["timers"] = std::move(timers);
    root["story"] = valueJson(state.story);
    if (state.hero)
    {
        const HeroState& h = *state.hero;
        root["hero"] = Json{{"character", characterJson(h.character)},
                            {"position", vec3(h.position)},
                            {"yaw", h.yaw},
                            {"torch", h.torch}};
    }
    root["currentWorld"] = state.currentWorld;
    Json worlds = Json::array();
    for (const WorldState& w : state.worlds)
    {
        Json removed = Json::array();
        for (const RemovedVob& r : w.delta.removed)
        {
            removed.push_back(Json{{"id", r.id.value}, {"name", r.name}});
        }
        Json changed = Json::array();
        for (const world::WorldFileVob& vob : w.delta.changed)
        {
            changed.push_back(vobJson(vob));
        }
        Json runtime = Json::array();
        for (const world::WorldFileVob& vob : w.delta.runtime)
        {
            runtime.push_back(vobJson(vob));
        }
        Json mobs = Json::array();
        for (const MobState& m : w.mobs)
        {
            mobs.push_back(Json{{"vob", m.vob},
                                {"definition", m.definition},
                                {"locked", m.locked},
                                {"open", m.open},
                                {"contents", m.contents}});
        }
        Json npcs = Json::array();
        for (const NpcState& n : w.npcs)
        {
            Json npc = Json{{"name", n.name},
                            {"instance", n.instance},
                            {"position", vec3(n.position)},
                            {"yaw", n.yaw},
                            {"routine", n.routine},
                            {"life", lifeName(n.life)},
                            {"downSeconds", n.downSeconds},
                            {"pickpocketed", n.pickpocketed},
                            {"summoned", n.summoned},
                            {"summonSeconds", n.summonSeconds}};
            if (n.character)
            {
                npc["character"] = characterJson(*n.character);
            }
            npcs.push_back(std::move(npc));
        }
        worlds.push_back(Json{{"path", w.path},
                              {"removed", std::move(removed)},
                              {"changed", std::move(changed)},
                              {"runtime", std::move(runtime)},
                              {"nextVobId", w.delta.nextVobId},
                              {"nextRuntimeVobId", w.delta.nextRuntimeVobId},
                              {"spentTriggers", w.spentTriggers},
                              {"mobs", std::move(mobs)},
                              {"npcs", std::move(npcs)}});
    }
    root["worlds"] = std::move(worlds);
    return root.dump(1) + "\n";
}

Result<GameState> fromJson(std::string_view text)
{
    const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Error{"game state: not a JSON object"};
    }
    Reader r;
    GameState s;
    if (!read(r, root, "version", "game state", s.version))
    {
        return Error{r.error};
    }
    if (s.version > kGameStateVersion)
    {
        return Error{
            std::format("game state version {} is newer than this game ({})", s.version, kGameStateVersion)};
    }
    // (Migrations of older versions go here.)
    bool ok = read(r, root, "clock", "game state", s.clock) &&
              read(r, root, "secondsPerMinute", "game state", s.secondsPerMinute) &&
              read(r, root, "scriptClock", "game state", s.scriptClock) &&
              read(r, root, "nextTimerId", "game state", s.nextTimerId) &&
              read(r, root, "currentWorld", "game state", s.currentWorld);
    const Json* timers = ok ? field(r, root, "timers", "game state") : nullptr;
    ok = ok && timers != nullptr && timers->is_array();
    for (usize i = 0; ok && i < timers->size(); ++i)
    {
        const std::string at = std::format("timers[{}]", i);
        script::ScriptVm::NamedTimer t;
        const Json& j = (*timers)[i];
        ok = read(r, j, "id", at, t.id) && read(r, j, "due", at, t.due) &&
             read(r, j, "interval", at, t.interval) && read(r, j, "function", at, t.function);
        const Json* args = ok ? field(r, j, "arguments", at) : nullptr;
        ok = ok && args != nullptr && args->is_array();
        for (usize k = 0; ok && k < args->size(); ++k)
        {
            script::Value v;
            ok = readValue(r, (*args)[k], std::format("{}.arguments[{}]", at, k), v);
            t.arguments.push_back(std::move(v));
        }
        s.timers.push_back(std::move(t));
    }
    const Json* story = ok ? field(r, root, "story", "game state") : nullptr;
    ok = ok && story != nullptr && readValue(r, *story, "story", s.story);
    if (ok && root.contains("hero"))
    {
        HeroState h;
        const Json& j = root["hero"];
        const Json* c = field(r, j, "character", "hero");
        ok = c != nullptr && readCharacter(r, *c, "hero.character", h.character) &&
             readVec3(r, j, "position", "hero", h.position) && read(r, j, "yaw", "hero", h.yaw) &&
             read(r, j, "torch", "hero", h.torch);
        s.hero = std::move(h);
    }
    const Json* worlds = ok ? field(r, root, "worlds", "game state") : nullptr;
    ok = ok && worlds != nullptr && worlds->is_array();
    for (usize i = 0; ok && i < worlds->size(); ++i)
    {
        const std::string at = std::format("worlds[{}]", i);
        const Json& j = (*worlds)[i];
        WorldState w;
        ok = read(r, j, "path", at, w.path) && read(r, j, "nextVobId", at, w.delta.nextVobId) &&
             read(r, j, "nextRuntimeVobId", at, w.delta.nextRuntimeVobId) &&
             read(r, j, "spentTriggers", at, w.spentTriggers) &&
             readVobs(r, j, "changed", at, w.delta.changed) && readVobs(r, j, "runtime", at, w.delta.runtime);
        const Json* removed = ok ? field(r, j, "removed", at) : nullptr;
        ok = ok && removed != nullptr && removed->is_array();
        for (usize k = 0; ok && k < removed->size(); ++k)
        {
            RemovedVob v;
            u64 id = 0;
            const std::string rk = std::format("{}.removed[{}]", at, k);
            ok = read(r, (*removed)[k], "id", rk, id) && read(r, (*removed)[k], "name", rk, v.name);
            v.id = world::VobId{id};
            w.delta.removed.push_back(std::move(v));
        }
        const Json* mobs = ok ? field(r, j, "mobs", at) : nullptr;
        ok = ok && mobs != nullptr && mobs->is_array();
        for (usize k = 0; ok && k < mobs->size(); ++k)
        {
            MobState m;
            const std::string mk = std::format("{}.mobs[{}]", at, k);
            const Json& mj = (*mobs)[k];
            ok = read(r, mj, "vob", mk, m.vob) && read(r, mj, "definition", mk, m.definition) &&
                 read(r, mj, "locked", mk, m.locked) && read(r, mj, "open", mk, m.open) &&
                 read(r, mj, "contents", mk, m.contents);
            w.mobs.push_back(std::move(m));
        }
        const Json* npcs = ok ? field(r, j, "npcs", at) : nullptr;
        ok = ok && npcs != nullptr && npcs->is_array();
        for (usize k = 0; ok && k < npcs->size(); ++k)
        {
            NpcState n;
            ok = readNpc(r, (*npcs)[k], std::format("{}.npcs[{}]", at, k), n);
            w.npcs.push_back(std::move(n));
        }
        s.worlds.push_back(std::move(w));
    }
    if (!ok)
    {
        return Error{r.error.empty() ? std::string("game state: malformed") : r.error};
    }
    return s;
}
} // namespace g7::save
