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

TEST_CASE("FramePacer: waits for the rest of the frame interval")
{
    g7::FramePacer pacer(100.0); // 10 ms
    CHECK(pacer.interval() == doctest::Approx(0.01));
    CHECK(pacer.secondsUntilNextFrame(0.0) == 0.0); // nothing started yet

    pacer.frameStarted(1.000);
    CHECK(pacer.secondsUntilNextFrame(1.003) == doctest::Approx(0.007));
    CHECK(pacer.secondsUntilNextFrame(1.020) == 0.0); // frame took longer than the interval
}

TEST_CASE("FramePacer: keeps the cadence without drift")
{
    g7::FramePacer pacer(100.0);
    g7::f64 now = 0.0;
    for (int frame = 0; frame < 1000; ++frame)
    {
        pacer.frameStarted(now);
        now += 0.004;                            // work
        now += pacer.secondsUntilNextFrame(now); // sleep exactly as long as asked
        now += frame % 3 == 0 ? 0.0005 : 0.0;    // occasional oversleep
    }
    // 1000 frames at 10 ms: oversleeping is compensated by the next frame, no accumulated drift.
    CHECK(now == doctest::Approx(10.0).epsilon(0.001));
}

TEST_CASE("FramePacer: resynchronises after a hitch instead of catching up")
{
    g7::FramePacer pacer(100.0);
    pacer.frameStarted(0.0);
    pacer.frameStarted(0.5); // 490 ms late (debugger, loading)
    CHECK(pacer.secondsUntilNextFrame(0.501) == doctest::Approx(0.009));
}

TEST_CASE("FramePacer: zero means unlimited")
{
    g7::FramePacer pacer(0.0);
    pacer.frameStarted(1.0);
    CHECK(pacer.interval() == 0.0);
    CHECK(pacer.secondsUntilNextFrame(1.0) == 0.0);
}
