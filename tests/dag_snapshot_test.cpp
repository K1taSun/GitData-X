#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "core/dag_snapshot.hpp"
#include "graph_fixture.hpp"

using gitdatax::core::DagSnapshot;
using gitdatax::core::HeadInfo;
using gitdatax::core::HeadKind;
using gitdatax::testing::make_node;
using gitdatax::testing::oid_from_number;

TEST_CASE("Pusty DagSnapshot", "[core][dag]") {
    const DagSnapshot snapshot({}, {}, {}, HeadInfo{});
    REQUIRE(snapshot.empty());
    REQUIRE(snapshot.size() == 0);
    REQUIRE(snapshot.head().kind == HeadKind::Unborn);
    REQUIRE(snapshot.find(oid_from_number(1)) == nullptr);
    REQUIRE(snapshot.children_of(oid_from_number(1)).empty());
}

TEST_CASE("DagSnapshot wyszukuje commity i dzieci", "[core][dag]") {
    const DagSnapshot snapshot(
        {make_node(1, {}, 100), make_node(2, {1}, 200), make_node(3, {1}, 300)}, {}, {},
        HeadInfo{});

    REQUIRE(snapshot.size() == 3);
    REQUIRE(snapshot.contains(oid_from_number(2)));
    REQUIRE_FALSE(snapshot.contains(oid_from_number(9)));

    const auto* node = snapshot.find(oid_from_number(2));
    REQUIRE(node != nullptr);
    REQUIRE(node->summary == "commit 2");

    const auto children = snapshot.children_of(oid_from_number(1));
    REQUIRE(children.size() == 2);
    REQUIRE(children[0] == oid_from_number(2));
    REQUIRE(children[1] == oid_from_number(3));
}

TEST_CASE("DagSnapshot zachowuje kolejnosc rodzicow merge", "[core][dag]") {
    const DagSnapshot snapshot({make_node(1, {}, 100), make_node(2, {}, 110), make_node(3, {}, 120),
                                make_node(4, {3, 1, 2}, 200)},
                               {}, {}, HeadInfo{});

    const auto* merge = snapshot.find(oid_from_number(4));
    REQUIRE(merge != nullptr);
    REQUIRE(merge->is_merge());
    REQUIRE(merge->parents.size() == 3);
    REQUIRE(merge->parents[0] == oid_from_number(3));
    REQUIRE(merge->parents[1] == oid_from_number(1));
    REQUIRE(merge->parents[2] == oid_from_number(2));
}

TEST_CASE("DagSnapshot odrzuca zduplikowane commity", "[core][dag]") {
    REQUIRE_THROWS_AS(DagSnapshot({make_node(1, {}, 100), make_node(1, {}, 200)}, {}, {}, HeadInfo{}),
                      std::invalid_argument);
}

TEST_CASE("DagSnapshot toleruje rodzica spoza zrzutu", "[core][dag]") {
    const DagSnapshot snapshot({make_node(2, {1}, 200)}, {}, {}, HeadInfo{});
    REQUIRE(snapshot.size() == 1);
    REQUIRE(snapshot.find(oid_from_number(2))->parents.size() == 1);
}

