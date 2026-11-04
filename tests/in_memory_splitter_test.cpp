#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "git/in_memory_splitter.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::git;
using gitdatax::testing::TempDir;

namespace {

Oid create_blob(git_repository* repo, const std::string& content) {
    git_oid oid{};
    REQUIRE(git_blob_create_from_buffer(&oid, repo, content.data(), content.size()) == 0);
    return Oid(oid);
}

Oid create_tree_with_entry(git_repository* repo, const std::string& filename, const Oid& blob_oid) {
    git_treebuilder* tb = nullptr;
    REQUIRE(git_treebuilder_new(&tb, repo, nullptr) == 0);
    REQUIRE(git_treebuilder_insert(nullptr, tb, filename.c_str(), &blob_oid.raw(), 0100644) == 0);
    git_oid tree_oid{};
    REQUIRE(git_treebuilder_write(&tree_oid, tb) == 0);
    git_treebuilder_free(tb);
    return Oid(tree_oid);
}

}  // namespace

TEST_CASE("InMemorySplitter: Puste drzewo i porownywanie drzew", "[git][splitter]") {
    GitLibInit init;
    TempDir dir;

    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo_owner(raw);

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    InMemorySplitter splitter(*repo);

    SECTION("empty_tree_oid tworzy poprawne puste drzewo") {
        const auto empty_oid = splitter.empty_tree_oid();
        REQUIRE(empty_oid.has_value());
        REQUIRE_FALSE(empty_oid->is_zero());

        git_tree* tree = nullptr;
        REQUIRE(git_tree_lookup(&tree, repo->handle(), &empty_oid->raw()) == 0);
        REQUIRE(git_tree_entrycount(tree) == 0);
        git_tree_free(tree);
    }

    SECTION("diff_trees wykrywa dodany plik") {
        const Oid empty_tree = splitter.empty_tree_oid().value();
        const Oid b1 = create_blob(repo->handle(), "zawartosc pliku testowego\n");
        const Oid tree1 = create_tree_with_entry(repo->handle(), "plik1.txt", b1);

        const auto deltas = splitter.diff_trees(empty_tree, tree1);
        REQUIRE(deltas.has_value());
        REQUIRE(deltas->size() == 1);
        REQUIRE((*deltas)[0].path == "plik1.txt");
        REQUIRE((*deltas)[0].is_new);
        REQUIRE((*deltas)[0].new_blob_oid == b1);
    }
}

TEST_CASE("InMemorySplitter: Bezdotykowa synteza drzew posrednich w RAM", "[git][splitter]") {
    GitLibInit init;
    TempDir dir;

    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo_owner(raw);

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    InMemorySplitter splitter(*repo);

    const Oid empty_tree = splitter.empty_tree_oid().value();
    const Oid blob_a = create_blob(repo->handle(), "Hello from A\n");
    const Oid blob_b = create_blob(repo->handle(), "Hello from B\n");

    // Krok 1: Dodajemy tylko plik A
    SplitStepAssignment step1{
        .step_index = 0,
        .completed_files = {
            FileDelta{.path = "a.txt", .new_blob_oid = blob_a, .filemode = 0100644}
        }
    };

    const auto tree_step1 = splitter.synthesize_intermediate_tree(empty_tree, step1, Oid{});
    REQUIRE(tree_step1.has_value());

    // Weryfikacja: drzewo 1 ma tylko a.txt
    git_tree* t1 = nullptr;
    REQUIRE(git_tree_lookup(&t1, repo->handle(), &tree_step1->raw()) == 0);
    TreePtr t1_owner(t1);
    REQUIRE(git_tree_entrycount(t1) == 1);
    REQUIRE(std::string(git_tree_entry_name(git_tree_entry_byindex(t1, 0))) == "a.txt");

    // Krok 2: Bazując na drzewie 1, dodajemy plik B
    SplitStepAssignment step2{
        .step_index = 1,
        .completed_files = {
            FileDelta{.path = "b.txt", .new_blob_oid = blob_b, .filemode = 0100644}
        }
    };

    const auto tree_step2 = splitter.synthesize_intermediate_tree(*tree_step1, step2, Oid{});
    REQUIRE(tree_step2.has_value());

    // Weryfikacja: drzewo 2 ma oba pliki: a.txt oraz b.txt
    git_tree* t2 = nullptr;
    REQUIRE(git_tree_lookup(&t2, repo->handle(), &tree_step2->raw()) == 0);
    TreePtr t2_owner(t2);
    REQUIRE(git_tree_entrycount(t2) == 2);

    // ZERO WORKING-TREE TOUCH: w folderze roboczym na dysku nie ma zadnych plikow a.txt ani b.txt!
    REQUIRE_FALSE(fs::exists(dir.path() / "a.txt"));
    REQUIRE_FALSE(fs::exists(dir.path() / "b.txt"));
}

