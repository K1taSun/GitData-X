#include <catch2/catch_test_macros.hpp>

#include "core/dag_verifier.hpp"
#include "graph_fixture.hpp"

using namespace gitdatax::core;
using gitdatax::git::Oid;
using gitdatax::testing::make_node;
using gitdatax::testing::oid_from_number;

TEST_CASE("DagVerifier: Poprawna transformacja 1 -> 1 (zmiana daty)", "[core][verifier]") {
    // Graf: 1 -> 2
    auto n1 = make_node(1, {}, 100);
    n1.tree_id = oid_from_number(1001);
    auto n2 = make_node(2, {1}, 200);
    n2.tree_id = oid_from_number(1002);

    const DagSnapshot orig_dag({n1, n2}, {}, {}, HeadInfo{});

    // Nowe commity (np. 101 i 102) z identycznymi drzewami, ale nowymi ID
    CommitNode new_101;
    new_101.id = oid_from_number(101);
    new_101.tree_id = n1.tree_id; // Boundary Tree Invariant zachowany
    new_101.parents = {};

    CommitNode new_102;
    new_102.id = oid_from_number(102);
    new_102.tree_id = n2.tree_id; // Boundary Tree Invariant zachowany
    new_102.parents = {oid_from_number(101)}; // Izomorfizm zachowany

    std::unordered_map<Oid, std::vector<Oid>> chain_map;
    chain_map[n1.id] = {new_101.id};
    chain_map[n2.id] = {new_102.id};

    std::unordered_map<Oid, CommitNode> new_commits;
    new_commits[new_101.id] = new_101;
    new_commits[new_102.id] = new_102;

    const auto res = DagVerifier::verify(orig_dag, chain_map, new_commits);
    REQUIRE(res.has_value());
}

TEST_CASE("DagVerifier: Poprawne rozbicie commitu 1 -> 3 (Split 1 -> K)", "[core][verifier]") {
    // Commit 1 jest rozbijany na 3 części: 101, 102, 103
    // Commit 2 jest dzieckiem commitu 1 i staje się commitem 104
    auto n1 = make_node(1, {}, 100);
    n1.tree_id = oid_from_number(1001);
    auto n2 = make_node(2, {1}, 200);
    n2.tree_id = oid_from_number(1002);

    const DagSnapshot orig_dag({n1, n2}, {}, {}, HeadInfo{});

    // Ogniwo 1/3
    CommitNode c101;
    c101.id = oid_from_number(101);
    c101.tree_id = oid_from_number(5001); // Pośrednie drzewo
    c101.parents = {};

    // Ogniwo 2/3
    CommitNode c102;
    c102.id = oid_from_number(102);
    c102.tree_id = oid_from_number(5002); // Pośrednie drzewo
    c102.parents = {c101.id};

    // Ogniwo 3/3 (ostatnie)
    CommitNode c103;
    c103.id = oid_from_number(103);
    c103.tree_id = n1.tree_id; // Złota reguła: dokładnie oryginalne drzewo!
    c103.parents = {c102.id};

    // Commit potomny 2 -> przepisywany jako 104, podpięty pod ostatnie ogniwo c103!
    CommitNode c104;
    c104.id = oid_from_number(104);
    c104.tree_id = n2.tree_id;
    c104.parents = {c103.id};

    std::unordered_map<Oid, std::vector<Oid>> chain_map;
    chain_map[n1.id] = {c101.id, c102.id, c103.id};
    chain_map[n2.id] = {c104.id};

    std::unordered_map<Oid, CommitNode> new_commits;
    new_commits[c101.id] = c101;
    new_commits[c102.id] = c102;
    new_commits[c103.id] = c103;
    new_commits[c104.id] = c104;

    const auto res = DagVerifier::verify(orig_dag, chain_map, new_commits);
    REQUIRE(res.has_value());
}

TEST_CASE("DagVerifier: Wykrywanie naruszenia Boundary Tree Invariant", "[core][verifier]") {
    auto n1 = make_node(1, {}, 100);
    n1.tree_id = oid_from_number(1001);
    const DagSnapshot orig_dag({n1}, {}, {}, HeadInfo{});

    CommitNode bad_c;
    bad_c.id = oid_from_number(101);
    bad_c.tree_id = oid_from_number(9999); // BŁĄD: Drzewo inne niż w n1!

    std::unordered_map<Oid, std::vector<Oid>> chain_map;
    chain_map[n1.id] = {bad_c.id};

    std::unordered_map<Oid, CommitNode> new_commits;
    new_commits[bad_c.id] = bad_c;

    const auto res = DagVerifier::verify(orig_dag, chain_map, new_commits);
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error().invariant_name == "Boundary Tree Invariant");
}

TEST_CASE("DagVerifier: Wykrywanie przerwania spojnosci lancucha 1 -> K", "[core][verifier]") {
    auto n1 = make_node(1, {}, 100);
    n1.tree_id = oid_from_number(1001);
    const DagSnapshot orig_dag({n1}, {}, {}, HeadInfo{});

    CommitNode c101;
    c101.id = oid_from_number(101);
    c101.tree_id = oid_from_number(5001);

    CommitNode c102;
    c102.id = oid_from_number(102);
    c102.tree_id = n1.tree_id;
    c102.parents = {}; // BŁĄD: Brak rodzica c101!

    std::unordered_map<Oid, std::vector<Oid>> chain_map;
    chain_map[n1.id] = {c101.id, c102.id};

    std::unordered_map<Oid, CommitNode> new_commits;
    new_commits[c101.id] = c101;
    new_commits[c102.id] = c102;

    const auto res = DagVerifier::verify(orig_dag, chain_map, new_commits);
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error().invariant_name == "Spojnosc lancucha");
}

TEST_CASE("DagVerifier: Wykrywanie bledu izomorfizmu rodzicow", "[core][verifier]") {
    auto n1 = make_node(1, {}, 100);
    n1.tree_id = oid_from_number(1001);
    auto n2 = make_node(2, {1}, 200);
    n2.tree_id = oid_from_number(1002);
    const DagSnapshot orig_dag({n1, n2}, {}, {}, HeadInfo{});

    CommitNode c101;
    c101.id = oid_from_number(101);
    c101.tree_id = n1.tree_id;

    CommitNode c102;
    c102.id = oid_from_number(102);
    c102.tree_id = n2.tree_id;
    c102.parents = {oid_from_number(999)}; // BŁĄD: Powinno byc c101, a jest 999!

    std::unordered_map<Oid, std::vector<Oid>> chain_map;
    chain_map[n1.id] = {c101.id};
    chain_map[n2.id] = {c102.id};

    std::unordered_map<Oid, CommitNode> new_commits;
    new_commits[c101.id] = c101;
    new_commits[c102.id] = c102;

    const auto res = DagVerifier::verify(orig_dag, chain_map, new_commits);
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error().invariant_name == "Izomorfizm krawedzi zewnetrznych");
}

