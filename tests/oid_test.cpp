#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "git/oid.hpp"

using gitdatax::git::GitLibInit;
using gitdatax::git::Oid;

namespace {
constexpr const char* kHexA = "0123456789abcdef0123456789abcdef01234567";
constexpr const char* kHexB = "fedcba9876543210fedcba9876543210fedcba98";
}  // namespace

TEST_CASE("Oid domyslnie jest zerowy", "[git][oid]") {
    const Oid oid;
    REQUIRE(oid.is_zero());
    REQUIRE(oid.to_hex() == std::string(40, '0'));
}

TEST_CASE("Oid from_hex i to_hex sa odwrotne", "[git][oid]") {
    GitLibInit init;
    const auto oid = Oid::from_hex(kHexA);
    REQUIRE(oid.has_value());
    REQUIRE_FALSE(oid->is_zero());
    REQUIRE(oid->to_hex() == kHexA);
}

TEST_CASE("Oid from_hex odrzuca zla dlugosc", "[git][oid]") {
    GitLibInit init;
    REQUIRE_FALSE(Oid::from_hex("").has_value());
    REQUIRE_FALSE(Oid::from_hex("abc").has_value());
    REQUIRE_FALSE(Oid::from_hex(std::string(41, 'a')).has_value());
}

TEST_CASE("Oid from_hex odrzuca znaki spoza hex", "[git][oid]") {
    GitLibInit init;
    const auto result = Oid::from_hex(std::string(40, 'z'));
    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(result.error().message.empty());
}

TEST_CASE("Oid porownania dzialaja", "[git][oid]") {
    GitLibInit init;
    const Oid a = Oid::from_hex(kHexA).value();
    const Oid b = Oid::from_hex(kHexB).value();
    const Oid a_copy = Oid::from_hex(kHexA).value();

    REQUIRE(a == a_copy);
    REQUIRE(a != b);
    REQUIRE(a < b);
    REQUIRE(b > a);
    REQUIRE((a <=> a_copy) == std::strong_ordering::equal);
}

TEST_CASE("Oid dziala jako klucz unordered_map", "[git][oid]") {
    GitLibInit init;
    std::unordered_map<Oid, int> map;
    map[Oid::from_hex(kHexA).value()] = 1;
    map[Oid::from_hex(kHexB).value()] = 2;

    REQUIRE(map.size() == 2);
    REQUIRE(map.at(Oid::from_hex(kHexA).value()) == 1);
    REQUIRE(map.at(Oid::from_hex(kHexB).value()) == 2);
}

TEST_CASE("Oid roundtrip hex dla losowych wartosci", "[git][oid][rapidcheck]") {
    GitLibInit init;
    const bool passed = rc::check("to_hex(from_hex(x)) == x", []() {
        const auto bytes = *rc::gen::container<std::vector<int>>(
            40, rc::gen::inRange(0, 16));
        static const char digits[] = "0123456789abcdef";
        std::string hex;
        for (const int d : bytes) {
            hex.push_back(digits[d]);
        }
        const auto oid = Oid::from_hex(hex);
        RC_ASSERT(oid.has_value());
        RC_ASSERT(oid->to_hex() == hex);
    });
    REQUIRE(passed);
}
