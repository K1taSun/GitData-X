#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "git/commit.hpp"
#include "git/error.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "git/transaction.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::git;
using gitdatax::core::CommitSignature;
using gitdatax::testing::TempDir;

namespace {

Oid create_commit_simple(git_repository* repo, const std::string& msg, const std::vector<Oid>& parents = {}) {
    git_treebuilder* tb = nullptr;
    REQUIRE(git_treebuilder_new(&tb, repo, nullptr) == 0);
    git_oid tree_oid{};
    REQUIRE(git_treebuilder_write(&tree_oid, tb) == 0);
    git_treebuilder_free(tb);

    CommitSignature sig{.name = "Test", .email = "t@t.com", .timestamp = 1000, .tz_offset_min = 0};
    CreateCommitParams params{
        .tree_id = Oid(tree_oid),
        .parent_ids = parents,
        .author = sig,
        .committer = sig,
        .message = msg
    };

    auto repo_obj = Repo::open_from_path(fs::path(git_repository_path(repo)).parent_path());
    REQUIRE(repo_obj.has_value());
    return create_commit_in_odb(*repo_obj, params).value();
}

}  // namespace

TEST_CASE("RefTransaction: Atomowa aktualizacja galezi i Detached HEAD", "[git][transaction]") {
    GitLibInit init;
    TempDir dir;

    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo_owner(raw);

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    const Oid c1 = create_commit_simple(repo->handle(), "c1\n");
    const Oid c2 = create_commit_simple(repo->handle(), "c2\n", {c1});
    const Oid c3 = create_commit_simple(repo->handle(), "c3\n", {c2});

    // Inicjalizacja gałęzi
    git_reference* ref_main = nullptr;
    REQUIRE(git_reference_create(&ref_main, repo->handle(), "refs/heads/main", &c1.raw(), 1, nullptr) == 0);
    git_reference_free(ref_main);

    git_reference* ref_feat = nullptr;
    REQUIRE(git_reference_create(&ref_feat, repo->handle(), "refs/heads/feat", &c2.raw(), 1, nullptr) == 0);
    git_reference_free(ref_feat);

    SECTION("Atomowy commit wielu galezi") {
        RefTransaction tx(*repo);
        REQUIRE(tx.add_branch_update(BranchUpdate{
            .ref_name = "refs/heads/main",
            .expected_old_oid = c1,
            .new_oid = c3
        }).has_value());

        REQUIRE(tx.add_branch_update(BranchUpdate{
            .ref_name = "refs/heads/feat",
            .expected_old_oid = c2,
            .new_oid = c3
        }).has_value());

        REQUIRE(tx.commit().has_value());

        // Weryfikacja: obie gałęzie wskazują na c3
        git_oid oid_main{};
        REQUIRE(git_reference_name_to_id(&oid_main, repo->handle(), "refs/heads/main") == 0);
        REQUIRE(Oid(oid_main) == c3);

        git_oid oid_feat{};
        REQUIRE(git_reference_name_to_id(&oid_feat, repo->handle(), "refs/heads/feat") == 0);
        REQUIRE(Oid(oid_feat) == c3);
    }

    SECTION("Wykrywanie konfliktu CAS przy modyfikacji w tle") {
        RefTransaction tx(*repo);

        // Zmiana gałęzi w tle przed weryfikacją CAS
        git_reference* temp = nullptr;
        REQUIRE(git_reference_create(&temp, repo->handle(), "refs/heads/main", &c2.raw(), 1, nullptr) == 0);
        git_reference_free(temp);

        // Transakcja oczekuje c1, ale gałąź wskazuje już c2
        const auto res = tx.add_branch_update(BranchUpdate{
            .ref_name = "refs/heads/main",
            .expected_old_oid = c1,
            .new_oid = c3
        });
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error().code == GIT_EMODIFIED);
    }

    SECTION("Aktualizacja HEAD w trybie Detached") {
        RefTransaction tx(*repo);
        tx.set_detached_head_target(c3);
        REQUIRE(tx.commit().has_value());

        REQUIRE(git_repository_head_detached(repo->handle()) == 1);
        git_oid head_id{};
        REQUIRE(git_reference_name_to_id(&head_id, repo->handle(), "HEAD") == 0);
        REQUIRE(Oid(head_id) == c3);
    }
}

