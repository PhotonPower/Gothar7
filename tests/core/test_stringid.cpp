#include <g7/core/StringId.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace g7;
using namespace g7::literals;

TEST_CASE("StringId: FNV-1a reference values")
{
    // Published 64-bit FNV-1a test vectors (lowercase input, so folding does not change them).
    CHECK(StringId::hashOf("") == 0xcbf29ce484222325ull);
    CHECK(StringId::hashOf("a") == 0xaf63dc4c8601ec8cull);
    CHECK(StringId::hashOf("foobar") == 0x85944171f73967e8ull);
}

TEST_CASE("StringId: case-insensitive, also at compile time")
{
    static_assert("wp_hc_campfire"_sid == "WP_HC_CAMPFIRE"_sid);
    static_assert("WP_HC_CAMPFIRE"_sid.valid());
    CHECK(StringId("WP_HC_CAMPFIRE") == "wp_hc_campfire"_sid);
    CHECK(StringId("Diego") == StringId("DIEGO"));
}

TEST_CASE("StringId: distinct names, invalid default")
{
    CHECK(StringId("PC_HERO") != StringId("PC_THIEF"));
    CHECK_FALSE(StringId().valid());
    CHECK(StringId() == StringId::fromHash(0));
    CHECK(StringId::fromHash(StringId::hashOf("x")) == StringId("X"));
}

TEST_CASE("StringId: usable in switch")
{
    const auto classify = [](StringId id)
    {
        switch (id.hash())
        {
        case "ITMW_SWORD"_sid.hash():
            return 1;
        case "ITAR_ARMOR"_sid.hash():
            return 2;
        default:
            return 0;
        }
    };
    CHECK(classify(StringId("ItMw_Sword")) == 1);
    CHECK(classify(StringId("ITAR_Armor")) == 2);
    CHECK(classify(StringId("ItFo_Apple")) == 0);
}

TEST_CASE("StringId: hash map key")
{
    std::unordered_map<StringId, int> counts;
    counts[StringId("FP_CAMPFIRE_01")] = 3;
    CHECK(counts.at("fp_campfire_01"_sid) == 3);
    CHECK(counts.count(StringId("FP_CAMPFIRE_02")) == 0);
}

#if G7_STRINGID_NAMES
TEST_CASE("StringId: debug names and formatting")
{
    const StringId id("WP_OC_Gate_Test");
    CHECK(id.name() == "WP_OC_Gate_Test");
    // First registered spelling wins.
    CHECK(StringId("wp_oc_gate_test").name() == "WP_OC_Gate_Test");
    CHECK(std::format("{}", id) == "WP_OC_Gate_Test");

    // Compile-time ids are not registered until created at runtime.
    constexpr StringId unregistered = "never_created_at_runtime_42"_sid;
    CHECK(unregistered.name().empty());
    CHECK(std::format("{}", unregistered) == std::format("#{:016x}", unregistered.hash()));
}

TEST_CASE("StringId: concurrent registration")
{
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            []
            {
                for (int i = 0; i < 500; ++i)
                {
                    const StringId id(std::string("THREAD_NAME_") + std::to_string(i));
                    CHECK(id.valid());
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    CHECK(StringId("thread_name_499").name() == "THREAD_NAME_499");
}
#endif
