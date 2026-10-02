#include <g7/core/Result.hpp>

#include <doctest/doctest.h>

TEST_CASE("Result holds value or error")
{
    g7::Result<int> ok = 42;
    CHECK(ok.ok());
    CHECK(ok.value() == 42);

    g7::Result<int> err = g7::Error{"broken"};
    CHECK_FALSE(err.ok());
    CHECK(err.error().message == "broken");

    g7::Result<void> voidOk;
    CHECK(voidOk.ok());
}
