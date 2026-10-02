#include <g7/core/Clock.hpp>

#include <doctest/doctest.h>

TEST_CASE("FixedStep accumulates and caps steps")
{
    g7::FixedStep step(0.1, 5);
    CHECK(step.advance(0.05) == 0);
    CHECK(step.advance(0.06) == 1); // 0.11 -> 1 step, 0.01 left
    CHECK(step.alpha() == doctest::Approx(0.1));
    CHECK(step.advance(10.0) == 5); // capped, remainder dropped
    CHECK(step.alpha() == doctest::Approx(0.0));
}
