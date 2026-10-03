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
             {"effects", Type::Table},
             {"tags", Type::StringList},
         },
         false});

    // Npc "npc_gate_guard" { name = "Torwache", level = 12, figure = "characters/figures/guard.figure.toml" }
    vm.defineKind({"Npc",
                   {
                       {"name", Type::String, true},
                       {"guild", Type::String},
                       {"level", Type::Integer, false, 0.0, 100.0}, // the hero starts at 0 (Gothic)
                       {"voice", Type::Integer, false, 0.0, 99.0},
                       {"figure", Type::String}, // figure manifest or .glb; default: a worker
                       {"attributes", Type::Table},
                       {"talents", Type::Table},
                       {"equipment", Type::StringList, false, none, none, "Item"},
                       {"inventory", Type::Table},
                       {"routine", Type::String, false, none, none, "Routine"},
                       {"protection", Type::Table}, // by damage type (edge, blunt, point, fire, magic, fall)
                       {"xp", Type::Integer, false, 0.0, none},
                       {"learn_points", Type::Integer, false, 0.0, none},
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

    // Routine "rtn_x" { { from = "08:00", to = "22:00", state = "zs_x", at = "wp_x" } }: entries checked with
    // M9.
    vm.defineKind({"Routine", {}, true});
}
} // namespace g7::gameplay
