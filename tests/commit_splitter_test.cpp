#include <catch2/catch_test_macros.hpp>

#include "core/commit_splitter.hpp"
#include "graph_fixture.hpp"

using namespace gitdatax::core;
using gitdatax::git::FileDelta;
using gitdatax::git::Oid;
using gitdatax::testing::make_node;
using gitdatax::testing::oid_from_number;

TEST_CASE("CommitSplitter: Blokowanie niepoprawnych operacji", "[core][splitter]") {
    auto merge_commit = make_node(1, {2, 3}, 1000); // Merge z dwoma rodzicami
    std::vector<FileDelta> deltas = {FileDelta{.path = "main.cpp"}};

    SECTION("Blokowanie podzialu commitu scalajacego (merge)") {
        const auto res = CommitSplitter::plan_file_split(merge_commit, deltas, 2);
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error().find("merge") != std::string::npos);
    }

    SECTION("Blokowanie K < 2") {
        auto normal_commit = make_node(1, {2}, 1000);
        const auto res = CommitSplitter::plan_file_split(normal_commit, deltas, 1);
        REQUIRE_FALSE(res.has_value());
    }

    SECTION("Blokowanie pustego diffa") {
        auto normal_commit = make_node(1, {2}, 1000);
        const auto res = CommitSplitter::plan_file_split(normal_commit, {}, 2);
        REQUIRE_FALSE(res.has_value());
    }
}

TEST_CASE("CommitSplitter: Rownomierny podzial na poziomie plikow", "[core][splitter]") {
    auto commit = make_node(10, {9}, 5000);
    commit.summary = "Dodanie nowego modulu";

    std::vector<FileDelta> deltas = {
        FileDelta{.path = "a.cpp"},
        FileDelta{.path = "b.cpp"},
        FileDelta{.path = "c.cpp"},
        FileDelta{.path = "d.cpp"}
    };

    const auto plan = CommitSplitter::plan_file_split(
        commit, deltas, 2, SplitDistributionMode::Uniform, "{orig_summary} [{k}/{K}]"
    );

    REQUIRE(plan.has_value());
    REQUIRE(plan->K == 2);
    REQUIRE(plan->steps.size() == 2);

    // Krok 1 (2 pliki)
    REQUIRE(plan->steps[0].completed_files.size() == 2);
    REQUIRE(plan->steps[0].completed_files[0].path == "a.cpp");
    REQUIRE(plan->steps[0].completed_files[1].path == "b.cpp");
    REQUIRE(plan->steps[0].message == "Dodanie nowego modulu [1/2]\n");

    // Krok 2 (2 pliki)
    REQUIRE(plan->steps[1].completed_files.size() == 2);
    REQUIRE(plan->steps[1].completed_files[0].path == "c.cpp");
    REQUIRE(plan->steps[1].completed_files[1].path == "d.cpp");
    REQUIRE(plan->steps[1].message == "Dodanie nowego modulu [2/2]\n");

    // Ostatni krok ma dokładnie oryginalny czas commitu
    REQUIRE(plan->steps[1].committer.timestamp == 5000);
    REQUIRE(plan->steps[0].committer.timestamp < 5000);
}

TEST_CASE("CommitSplitter: Semantyczny podzial (naglowki -> kod -> testy)", "[core][splitter]") {
    auto commit = make_node(1, {}, 1000);
    commit.summary = "Refaktoryzacja bazy";

    std::vector<FileDelta> deltas = {
        FileDelta{.path = "tests/db_test.cpp"}, // test (score 3)
        FileDelta{.path = "src/db.cpp"},        // source (score 2)
        FileDelta{.path = "include/db.hpp"}     // header (score 1)
    };

    const auto plan = CommitSplitter::plan_file_split(
        commit, deltas, 3, SplitDistributionMode::SemanticHeadersSourceTests
    );

    REQUIRE(plan.has_value());
    REQUIRE(plan->steps.size() == 3);

    // Krok 1: Nagłówek
    REQUIRE(plan->steps[0].completed_files.front().path == "include/db.hpp");
    // Krok 2: Źródło
    REQUIRE(plan->steps[1].completed_files.front().path == "src/db.cpp");
    // Krok 3: Testy
    REQUIRE(plan->steps[2].completed_files.front().path == "tests/db_test.cpp");
}

