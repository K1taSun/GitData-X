#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <algorithm>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/dag_snapshot.hpp"
#include "graph_fixture.hpp"

using gitdatax::core::CommitNode;
using gitdatax::core::DagSnapshot;
using gitdatax::core::HeadInfo;
using gitdatax::git::Oid;
using gitdatax::testing::every_parent_before_child;
using gitdatax::testing::make_node;
using gitdatax::testing::oid_from_number;

namespace {

DagSnapshot make_snapshot(std::vector<CommitNode> nodes) {
    return DagSnapshot(std::move(nodes), {}, {}, HeadInfo{});
}

// Graf z rozwidleniami i 3 merge commitami (6, 9 i ośmiornicowy 11).
std::vector<CommitNode> forked_graph_with_three_merges() {
    return {
        make_node(1, {}, 100),          // korzeń
        make_node(2, {1}, 110),         // gałąź A
        make_node(3, {2}, 120),
        make_node(4, {1}, 115),         // gałąź B
        make_node(5, {4}, 125),
        make_node(6, {3, 5}, 200),      // merge #1
        make_node(7, {6}, 210),         // rozwidlenie po merge
        make_node(8, {6}, 220),
        make_node(9, {7, 8}, 300),      // merge #2
        make_node(10, {1}, 130),        // gałąź E
        make_node(11, {9, 10, 4}, 400)  // merge #3, ośmiornica (3 rodziców)
    };
}

}  // namespace

TEST_CASE("Kahn: pusty graf daje pusty wynik", "[core][kahn]") {
    const auto sorted = make_snapshot({}).topological_sort_oldest_first();
    REQUIRE(sorted.has_value());
    REQUIRE(sorted->empty());
}

TEST_CASE("Kahn: rodzice przed dziecmi w grafie z 3 merge", "[core][kahn]") {
    const auto snapshot = make_snapshot(forked_graph_with_three_merges());

    const auto sorted = snapshot.topological_sort_oldest_first();

    REQUIRE(sorted.has_value());
    REQUIRE(sorted->size() == 11);
    REQUIRE(every_parent_before_child(snapshot, *sorted));
    REQUIRE(sorted->front() == oid_from_number(1));
    REQUIRE(sorted->back() == oid_from_number(11));
}

TEST_CASE("Kahn: wynik nie zalezy od kolejnosci wejscia", "[core][kahn]") {
    const auto reference =
        make_snapshot(forked_graph_with_three_merges()).topological_sort_oldest_first().value();

    std::mt19937 engine(12345);
    for (int round = 0; round < 20; ++round) {
        auto nodes = forked_graph_with_three_merges();
        std::shuffle(nodes.begin(), nodes.end(), engine);
        const auto sorted = make_snapshot(std::move(nodes)).topological_sort_oldest_first();
        REQUIRE(sorted.has_value());
        REQUIRE(*sorted == reference);
    }
}

TEST_CASE("Kahn: rodzic z pozniejszym czasem nadal jest pierwszy", "[core][kahn]") {
    // Zegar maszyny bywa przestawiony, więc czas dziecka może być wcześniejszy niż rodzica.
    const auto snapshot = make_snapshot({make_node(2, {1}, 100), make_node(1, {}, 500)});

    const auto sorted = snapshot.topological_sort_oldest_first();

    REQUIRE(sorted.has_value());
    REQUIRE(*sorted == std::vector<Oid>{oid_from_number(1), oid_from_number(2)});
}

TEST_CASE("Kahn: remis czasu rozstrzyga Oid", "[core][kahn]") {
    const auto snapshot = make_snapshot({make_node(2, {}, 100), make_node(1, {}, 100)});

    const auto sorted = snapshot.topological_sort_oldest_first();

    REQUIRE(sorted.has_value());
    REQUIRE(*sorted == std::vector<Oid>{oid_from_number(1), oid_from_number(2)});
}

TEST_CASE("Kahn: cykl zwraca blad", "[core][kahn]") {
    const auto snapshot = make_snapshot({make_node(1, {2}, 100), make_node(2, {1}, 200)});

    const auto sorted = snapshot.topological_sort_oldest_first();

    REQUIRE_FALSE(sorted.has_value());
    REQUIRE_FALSE(sorted.error().empty());
}

TEST_CASE("Kahn: rodzic spoza zrzutu jest pomijany", "[core][kahn]") {
    const auto snapshot = make_snapshot({make_node(2, {1}, 200), make_node(3, {2}, 300)});

    const auto sorted = snapshot.topological_sort_oldest_first();

    REQUIRE(sorted.has_value());
    REQUIRE(*sorted == std::vector<Oid>{oid_from_number(2), oid_from_number(3)});
}

TEST_CASE("Kahn: losowe grafy DAG zawsze maja poprawny porzadek", "[core][kahn][rapidcheck]") {
    const bool passed = rc::check("rodzice przed dziecmi", []() {
        const int count = *rc::gen::inRange(1, 30);
        std::vector<CommitNode> nodes;
        for (int i = 0; i < count; ++i) {
            // Rodzice tylko z wcześniejszych indeksów gwarantują brak cykli.
            std::set<unsigned int> parents;
            const int wanted = i == 0 ? 0 : *rc::gen::inRange(0, 4);
            for (int k = 0; k < wanted; ++k) {
                parents.insert(static_cast<unsigned int>(*rc::gen::inRange(0, i)) + 1);
            }
            const auto time = *rc::gen::inRange(0, 1000);
            nodes.push_back(make_node(static_cast<unsigned int>(i) + 1,
                                      std::vector<unsigned int>(parents.begin(), parents.end()), time));
        }
        std::mt19937 engine(*rc::gen::arbitrary<unsigned int>());
        std::shuffle(nodes.begin(), nodes.end(), engine);

        const auto snapshot = make_snapshot(std::move(nodes));
        const auto sorted = snapshot.topological_sort_oldest_first();
        RC_ASSERT(sorted.has_value());
        RC_ASSERT(every_parent_before_child(snapshot, *sorted));
    });
    REQUIRE(passed);
}

TEST_CASE("DagSnapshot mozna czytac z wielu watkow", "[core][kahn][threads]") {
    const auto snapshot = make_snapshot(forked_graph_with_three_merges());
    const auto reference = snapshot.topological_sort_oldest_first().value();

    constexpr int kThreads = 8;
    std::vector<int> failures(kThreads, 0);  // każdy wątek pisze tylko do swojego elementu
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&snapshot, &reference, &failures, t]() {
            for (int round = 0; round < 200; ++round) {
                const auto sorted = snapshot.topological_sort_oldest_first();
                if (!sorted.has_value() || *sorted != reference ||
                    snapshot.find(oid_from_number(6)) == nullptr ||
                    snapshot.children_of(oid_from_number(6)).size() != 2) {
                    ++failures[static_cast<std::size_t>(t)];
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    for (const int failed : failures) {
        REQUIRE(failed == 0);
    }
}
