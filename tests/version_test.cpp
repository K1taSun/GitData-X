#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include "core/version.hpp"

TEST_CASE("version() zwraca niepusty numer wersji", "[version]") {
    REQUIRE_FALSE(gitdatax::version().empty());
    REQUIRE(gitdatax::version() == "0.1.0");
}

TEST_CASE("RapidCheck jest podlaczony do Catch2", "[rapidcheck]") {
    // Prosta własność: dodawanie liczb całkowitych jest przemienne.
    const bool passed = rc::check("a + b == b + a", [](short a, short b) {
        RC_ASSERT(a + b == b + a);
    });
    REQUIRE(passed);
}

