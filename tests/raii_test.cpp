#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

#include "git/error.hpp"
#include "git/raii.hpp"
#include "temp_dir.hpp"

using namespace gitdatax::git;

TEST_CASE("Wrappery RAII nie sa kopiowalne", "[git][raii]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<RepositoryPtr>);
    STATIC_REQUIRE(std::is_move_constructible_v<RepositoryPtr>);
}

TEST_CASE("RepositoryPtr zwalnia repozytorium", "[git][raii]") {
    GitLibInit init;
    gitdatax::testing::TempDir dir;

    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo(raw);

    REQUIRE(repo != nullptr);
    REQUIRE(git_repository_is_bare(repo.get()) == 0);
}

TEST_CASE("Puste wskazniki RAII sa bezpieczne", "[git][raii]") {
    // Deleter przyjmuje nullptr, bo unique_ptr nie wywołuje go dla pustego wskaźnika.
    RepositoryPtr repo;
    CommitPtr commit;
    TreePtr tree;
    IndexPtr index;
    DiffPtr diff;
    PatchPtr patch;
    RevwalkPtr walk;
    SignaturePtr signature;
    TransactionPtr transaction;
    ReferencePtr reference;
    REQUIRE(repo == nullptr);
    REQUIRE(reference == nullptr);
}

TEST_CASE("SignaturePtr, IndexPtr i RevwalkPtr zwalniaja zasoby", "[git][raii]") {
    GitLibInit init;
    gitdatax::testing::TempDir dir;

    git_repository* raw_repo = nullptr;
    REQUIRE(git_repository_init(&raw_repo, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo(raw_repo);

    git_signature* raw_signature = nullptr;
    REQUIRE(git_signature_new(&raw_signature, "Jan", "jan@example.com", 1700000000, 60) == 0);
    SignaturePtr signature(raw_signature);

    git_index* raw_index = nullptr;
    REQUIRE(git_index_new(&raw_index) == 0);
    IndexPtr index(raw_index);

    git_revwalk* raw_walk = nullptr;
    REQUIRE(git_revwalk_new(&raw_walk, repo.get()) == 0);
    RevwalkPtr walk(raw_walk);

    REQUIRE(std::string(signature->name) == "Jan");
    REQUIRE(git_index_entrycount(index.get()) == 0);
}
