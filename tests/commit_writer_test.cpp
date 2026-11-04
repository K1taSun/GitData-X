#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "git/commit.hpp"
#include "git/error.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::git;
using gitdatax::core::CommitSignature;
using gitdatax::testing::TempDir;

namespace {

Oid create_empty_tree(git_repository* repo) {
    git_treebuilder* tb = nullptr;
    REQUIRE(git_treebuilder_new(&tb, repo, nullptr) == 0);
    git_oid tree_oid{};
    REQUIRE(git_treebuilder_write(&tree_oid, tb) == 0);
    git_treebuilder_free(tb);
    return Oid(tree_oid);
}

}  // namespace

TEST_CASE("CommitWriter: Zapis commitu bezposrednio do ODB", "[git][commit]") {
    GitLibInit init;
    TempDir dir;

    auto repo = Repo::open_from_path(dir.path());
    // Inicjalizujemy repozytorium
    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo_owner(raw);
    repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    const Oid tree = create_empty_tree(repo->handle());

    CommitSignature author{
        .name = "Autor Testowy",
        .email = "autor@example.com",
        .timestamp = 1794355000,
        .tz_offset_min = 60
    };

    CommitSignature committer{
        .name = "Committer Testowy",
        .email = "committer@example.com",
        .timestamp = 1794355100,
        .tz_offset_min = 60
    };

    SECTION("Commit korzenia (0 rodzicow) tworzy poprawny obiekt ODB") {
        CreateCommitParams params{
            .tree_id = tree,
            .parent_ids = {},
            .author = author,
            .committer = committer,
            .message = "Inicjalny commit w ODB\n"
        };

        const auto commit_oid = create_commit_in_odb(*repo, params);
        REQUIRE(commit_oid.has_value());
        REQUIRE_FALSE(commit_oid->is_zero());

        // Weryfikacja: odczytujemy commit bezpośrednio z ODB
        git_commit* read_commit = nullptr;
        REQUIRE(git_commit_lookup(&read_commit, repo->handle(), &commit_oid->raw()) == 0);
        CommitPtr commit_ptr(read_commit);

        REQUIRE(Oid(*git_commit_tree_id(read_commit)) == tree);
        REQUIRE(git_commit_parentcount(read_commit) == 0);
        REQUIRE(std::string(git_commit_message(read_commit)) == "Inicjalny commit w ODB\n");

        const git_signature* sig = git_commit_author(read_commit);
        REQUIRE(std::string(sig->name) == "Autor Testowy");
        REQUIRE(sig->when.time == 1794355000);
        REQUIRE(sig->when.offset == 60);
    }

    SECTION("Commit z wieloma rodzicami (merge) zachowuje ich kolejnosc") {
        CreateCommitParams p1{
            .tree_id = tree, .parent_ids = {},
            .author = author, .committer = committer, .message = "r1\n"
        };
        const auto r1 = create_commit_in_odb(*repo, p1).value();

        CreateCommitParams p2{
            .tree_id = tree, .parent_ids = {},
            .author = author, .committer = committer, .message = "r2\n"
        };
        const auto r2 = create_commit_in_odb(*repo, p2).value();

        CreateCommitParams merge_params{
            .tree_id = tree, .parent_ids = {r1, r2},
            .author = author, .committer = committer, .message = "merge\n"
        };
        const auto merge_oid = create_commit_in_odb(*repo, merge_params).value();

        git_commit* read_merge = nullptr;
        REQUIRE(git_commit_lookup(&read_merge, repo->handle(), &merge_oid.raw()) == 0);
        CommitPtr merge_ptr(read_merge);

        REQUIRE(git_commit_parentcount(read_merge) == 2);
        REQUIRE(Oid(*git_commit_parent_id(read_merge, 0)) == r1);
        REQUIRE(Oid(*git_commit_parent_id(read_merge, 1)) == r2);
    }
}

