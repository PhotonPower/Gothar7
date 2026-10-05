#include <g7/gameplay/Character.hpp>
#include <g7/gameplay/ScriptContent.hpp>

namespace g7::gameplay
{
void defineContentKinds(script::ScriptVm& vm)
{
    using Type = script::FieldSpec::Type;
    using script::FieldSpec;
    constexpr auto none = std::nullopt;
    std::vector<std::string> categories(kItemCategories.begin(), kItemCategories.end());

    // Item "it_sword_old" { name = "Altes Schwert", category = "melee_1h", value = 40, ... }
    vm.defineKind(
        {"Item",
         {
             {"name", Type::String, true},
             {"description", Type::String},
             {"category", Type::String, false, none, none, {}, categories}, // kItemCategories; default misc
             {"value", Type::Integer, false, 0.0, 1'000'000.0},
             {"weight", Type::Number, false, 0.0, 1000.0},
             {"mesh", Type::String}, // model in the VFS; without one a placeholder by category
             {"damage", Type::Table},
             {"protection", Type::Table},
             {"requires", Type::Table},
             {"effects", Type::Table},   // using it: { hp = 20, mana = 10 } (food, potions)
             {"text", Type::String},     // documents: what reading shows
             {"on_use", Type::Function}, // fn(item): after the effects
             {"tags", Type::StringList},
         },
         false});

    // Npc "npc_gate_guard" { name = "Torwache", level = 12, figure = "characters/figures/guard.figure.toml" }
    vm.defineKind(
        {"Npc",
         {
             {"name", Type::String, true},
             {"guild", Type::String},
             {"level", Type::Integer, false, 0.0, 100.0}, // the hero starts at 0 (Gothic)
             {"gender", Type::String}, // "m" (default) or "f": the voice of its shouts (data/voices.lua)
             {"voice", Type::String},  // voice group of its shouts; default: its guild (data/voices.lua)
             {"figure", Type::String}, // figure manifest or .glb; default: a worker
             {"attributes", Type::Table},
             {"talents", Type::Table},
             {"equipment", Type::StringList, false, none, none, "Item"},
             {"inventory", Type::Table},
             {"routine", Type::String, false, none, none, "Routine"},
             {"senses", Type::Table},      // sight (m), angle (degrees), hearing (factor); M9
             {"species", Type::String},    // animals: wolf, keiler, laufvogel (M9 part D)
             {"damage", Type::Table},      // animals: their bite or blow by type, plus strength (M11)
             {"figure_set", Type::String}, // nameless people: a figure of this set (figure_sets.toml)
             {"protection", Type::Table},  // by damage type (edge, blunt, point, fire, magic, fall)
             {"xp", Type::Integer, false, 0.0, none},
             {"learn_points", Type::Integer, false, 0.0, none},
             // Pickpocketing (Gothic 1): the dexterity needed; default Pickpocketing.default_dex
             {"pickpocket_dex", Type::Integer, false, 0.0, 200.0},
             // What a successful pickpocket takes first while the NPC has it (a quest item; M10)
             {"pickpocket_item", Type::String, false, none, none, "Item"},
         },
         false});

    // Info "dia_gate_guard_hello" { npc = "npc_gate_guard", condition = function ... end, run = ... }
    vm.defineKind({"Info",
                   {
                       {"npc", Type::String, true, none, none, "Npc"},
                       {"nr", Type::Integer, false, 0.0, 9999.0}, // order in the dialog menu
                       {"description", Type::String}, // menu text; important infos start by themselves
                       {"important", Type::Boolean},
                       {"permanent", Type::Boolean},
                       {"approach", Type::Boolean}, // important: the NPC walks up to the player (M10)
                       {"trade", Type::Boolean},    // after its lines: the trade screen (M10 part C)
                       {"condition", Type::Function},
                       {"run", Type::Function, true},
                   },
                   false});

    // Quest "quest_lost_sheep" { name = "Das verlorene Schaf", description = "..." }
    vm.defineKind({"Quest",
                   {
                       {"name", Type::String, true},
                       {"description", Type::String},
                       {"topic", Type::String},
                   },
                   false});

    // Mob "mob_chest_hut" { name = "Truhe", type = "chest", lock = "LRRL", key = "it_key_chest_hut",
    //                       contents = { it_apple = 2 } }: what a mob vob (components.mob.definition) is;
    //                       type is a
    // mob type of data/mobs.toml (slots, clips). M8 part C.
    vm.defineKind({"Mob",
                   {
                       {"name", Type::String, true}, // the focus name
                       {"type", Type::String, true}, // chest, door, anvil, bed ... (data/mobs.toml)
                       {"lock", Type::String},       // combination of L and R; empty or missing: not locked
                       {"key", Type::String, false, none, none, "Item"},
                       {"contents", Type::Table},  // chests: { it_x = count }
                       {"owner", Type::String},    // Npc or guild (part D)
                       {"on_use", Type::Function}, // fn(mob: string): when a user is at the mob (loop)
                   },
                   false});

    // Recipe "rcp_sword_crude" { name = "Grobes Schwert", mob = "anvil", takes = { it_blank_hot = 1 },
    //                            gives = { it_sword_crude = 1 }, strikes = 3 }: what can be made at a mob
    //                            type
    // (anvil: forging; later frying, brewing). M8 part C2.
    vm.defineKind({"Recipe",
                   {
                       {"name", Type::String, true},
                       {"mob", Type::String, true},  // mob type of data/mobs.toml
                       {"takes", Type::Table, true}, // { it_x = count }
                       {"gives", Type::Table, true},
                       {"strikes", Type::Integer, false, 1.0, 20.0}, // anvil: hammer blows (default 3)
                   },
                   false});

    // State "zs_x" { begin = fn(npc, at), loop = fn(npc, seconds) -> "done"|nil, finish = fn(npc) }: what an
    // NPC does in a routine entry (Gothic's ZS_). begin queues commands (npc_goto, npc_play ...); loop runs
    // every half second while the queue is empty; "done" ends the state. M9 part B.
    vm.defineKind({"State",
                   {
                       {"begin", Type::Function},
                       {"loop", Type::Function},
                       {"finish", Type::Function},
                   },
                   false});

    // Routine "rtn_x" { { from = "08:00", to = "22:00", state = "zs_x", at = "wp_x" } }: entries checked with
    // M9.
    vm.defineKind({"Routine", {}, true});
}
} // namespace g7::gameplay
