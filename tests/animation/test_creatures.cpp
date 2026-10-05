// Animals (M6 part D3): the graphs data/anim/<species>.animgraph.toml with the real rigs and clip sets
// (characters-pipeline.md §7.1) - root motion forward and turning, one-shot actions, dying.

#include <g7/animation/Animator.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using namespace g7::animation;

namespace
{
std::vector<u8> read(const std::string& relative)
{
    auto bytes = fs::readFile(fs::fromUtf8(std::string(G7_ASSET_SOURCE_DIR) + "/" + relative));
    REQUIRE_MESSAGE(bytes.ok(), relative);
    return std::move(bytes).value();
}

std::string text(const std::string& relative)
{
    const std::vector<u8> bytes = read(relative);
    return std::string(bytes.begin(), bytes.end());
}

constexpr f32 kStep = 1.0f / 60.0f;
} // namespace

TEST_CASE("Animals: the graphs of wolf, keiler and laufvogel with their rigs and clips")
{
    for (const std::string species : {"wolf", "keiler", "laufvogel"})
    {
        CAPTURE(species);
        const std::string folder = "characters/monsters/" + species;
        auto rig = asset::loadSkinnedGltf(read(folder + "/rig/" + species + "_reference.glb"), {}, species);
        REQUIRE_MESSAGE(rig.ok(), (rig.ok() ? "" : rig.error().message));
        const Skeleton skeleton = Skeleton::create(rig.value().skeleton).value();
        auto set = asset::loadAnimationGltf(read(folder + "/anims/" + species + ".glb"), {}, species);
        REQUIRE(set.ok());
        REQUIRE(
            asset::applyClipEvents(set.value(), text(folder + "/anims/" + species + ".events.toml"), species)
                .ok());
        auto graph = AnimGraph::parse(text("data/anim/" + species + ".animgraph.toml"), species);
        REQUIRE_MESSAGE(graph.ok(), (graph.ok() ? "" : graph.error().message));
        const asset::AnimationSetData* sets[] = {&set.value()};
        auto created = Animator::create(graph.value(), skeleton, sets);
        REQUIRE_MESSAGE(created.ok(), (created.ok() ? "" : created.error().message));
        Animator a = std::move(created).value();
        CHECK(a.state() == "move");

        // Standing: no root motion.
        Vec3 moved(0.0f);
        for (int i = 0; i < 60; ++i)
        {
            a.update(kStep);
            moved += a.rootMotion();
        }
        CHECK(glm::length(moved) < 0.02f);

        // Walking and running: the root moves ahead (+Z), faster when running; the drawn root stays.
        const auto travel = [&](f32 speed)
        {
            a.setFloat("speed", speed);
            for (int i = 0; i < 30; ++i) // into the blend
            {
                a.update(kStep);
            }
            Vec3 sum(0.0f);
            for (int i = 0; i < 120; ++i)
            {
                a.update(kStep);
                sum += a.rootMotion();
            }
            return sum / 2.0f; // m/s
        };
        // At every blend point the root moves at the point's speed: the points are the clips' own speeds
        // (figuren #200: wolf 1.19 / 2.99 / 6.00, keiler 0.99 / 4.98, laufvogel 1.30 / 6.50), so the feet do
        // not slide.
        f32 previous = 0.0f;
        usize gaits = 0;
        for (const auto& [value, clip] : graph.value().states[0].points)
        {
            if (value <= 0.0f)
            {
                continue;
            }
            CAPTURE(clip);
            const Vec3 v = travel(value);
            CHECK(v.z == doctest::Approx(value).epsilon(0.1));
            CHECK(v.z > previous);
            CHECK(std::abs(v.x) < 0.25f * v.z); // straight on
            previous = v.z;
            ++gaits;
        }
        CHECK(gaits == (species == "wolf" ? 3u : 2u)); // the wolf trots as well
        const i32 root = skeleton.find("root");
        REQUIRE(root >= 0);
        CHECK(glm::length(a.pose()[static_cast<usize>(root)].translation -
                          skeleton.restPose()[static_cast<usize>(root)].translation) < 1e-5f);
        a.setFloat("speed", 0.0f);
        for (int i = 0; i < 60; ++i)
        {
            a.update(kStep);
        }

        // Turning on the spot: left is positive, by at least 45 degrees, then back to moving.
        for (const auto& [turn, sign] : {std::pair{-1.0f, 1.0f}, std::pair{1.0f, -1.0f}})
        {
            a.setFloat("turn", turn);
            a.update(kStep);
            CHECK(a.state() == (turn < 0.0f ? "turn_l" : "turn_r"));
            a.setFloat("turn", 0.0f);
            f32 yaw = a.rootMotionYaw();
            for (int i = 0; i < 120 && a.state() != "move"; ++i)
            {
                a.update(kStep);
                yaw += a.rootMotionYaw();
            }
            CHECK(glm::degrees(yaw) * sign > 45.0f);
            CHECK(a.state() == "move");
        }

        // One-shot actions return to moving; eating and sleeping last while set.
        for (const auto& [action, state] : {std::pair{1.0f, "attack_1"}, std::pair{2.0f, "attack_2"},
                                            std::pair{3.0f, "hit"}, std::pair{4.0f, "threaten"}})
        {
            a.setFloat("action", action);
            a.update(kStep);
            CHECK(a.state() == state);
            a.setFloat("action", 0.0f);
            for (int i = 0; i < 180 && a.state() != "move"; ++i)
            {
                a.update(kStep);
            }
            CHECK(a.state() == "move");
        }
        a.setBool("eat", true);
        for (int i = 0; i < 180; ++i)
        {
            a.update(kStep);
        }
        CHECK(a.state() == "eat");
        a.setBool("eat", false);
        a.setBool("sleep", true);
        for (int i = 0; i < 2; ++i)
        {
            a.update(kStep);
        }
        CHECK(a.state() == "sleep");
        a.setBool("sleep", false);

        // Dying: stays down, a hit does not wake it.
        a.setBool("dead", true);
        a.update(kStep);
        CHECK(a.state() == "die");
        a.setFloat("action", 3.0f);
        for (int i = 0; i < 240; ++i)
        {
            a.update(kStep);
        }
        CHECK(a.state() == "die");
        CHECK(a.stateEnded());
    }
}
