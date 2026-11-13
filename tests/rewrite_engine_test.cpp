#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "core/backup_manager.hpp"
#include "core/commit_splitter.hpp"
#include "core/dag_snapshot.hpp"
#include "core/rewrite_engine.hpp"
#include "core/rewrite_planner.hpp"
#include "git/commit.hpp"
#include "git/graph_loader.hpp"
#include "git/in_memory_splitter.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::core;
using namespace gitdatax::git;
using gitdatax::testing::TempDir;

namespace {

Oid create_blob_data(git_repository* repo, const std::string& data) {
    git_oid oid{};
    REQUIRE(git_blob_create_from_buffer(&oid, repo, data.data(), data.size()) == 0);
    return Oid(oid);
}

Oid insert_path_into_tree(git_repository* repo, git_treebuilder* root_tb,
                          const std::string& path, const Oid& blob) {
    const auto slash = path.find('/');
    if (slash == std::string::npos) {
        REQUIRE(git_treebuilder_insert(nullptr, root_tb, path.c_str(), &blob.raw(), GIT_FILEMODE_BLOB) == 0);
        return blob;
    }

    const std::string dir = path.substr(0, slash);
    const std::string file = path.substr(slash + 1);
    git_treebuilder* sub_tb = nullptr;
    REQUIRE(git_treebuilder_new(&sub_tb, repo, nullptr) == 0);
    REQUIRE(git_treebuilder_insert(nullptr, sub_tb, file.c_str(), &blob.raw(), GIT_FILEMODE_BLOB) == 0);
    git_oid sub_oid{};
    REQUIRE(git_treebuilder_write(&sub_oid, sub_tb) == 0);
    git_treebuilder_free(sub_tb);

    REQUIRE(git_treebuilder_insert(nullptr, root_tb, dir.c_str(), &sub_oid, GIT_FILEMODE_TREE) == 0);
    return Oid(sub_oid);
}

Oid create_tree_two_files(git_repository* repo, const std::string& f1, const Oid& b1,
                          const std::string& f2, const Oid& b2) {
    git_treebuilder* tb = nullptr;
    REQUIRE(git_treebuilder_new(&tb, repo, nullptr) == 0);
    insert_path_into_tree(repo, tb, f1, b1);
    insert_path_into_tree(repo, tb, f2, b2);
    git_oid tree_oid{};
    REQUIRE(git_treebuilder_write(&tree_oid, tb) == 0);
    git_treebuilder_free(tb);
    return Oid(tree_oid);
}

}  // namespace

TEST_CASE("RewriteEngine: Pelne przepisanie kaskadowe 1 -> 1 i Undo", "[core][engine][integration]") {
    GitLibInit init;
    TempDir dir;

    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo_owner(raw);

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    InMemorySplitter splitter(*repo);
    const Oid empty_tree = splitter.empty_tree_oid().value();

    CommitSignature sig{.name = "Dev", .email = "dev@example.com", .timestamp = 1000, .tz_offset_min = 60};

    // Budujemy 3 commity: c1 -> c2 -> c3
    const Oid c1 = create_commit_in_odb(*repo, CreateCommitParams{
        .tree_id = empty_tree, .parent_ids = {}, .author = sig, .committer = sig, .message = "c1\n"
    }).value();

    sig.timestamp = 1100;
    const Oid c2 = create_commit_in_odb(*repo, CreateCommitParams{
        .tree_id = empty_tree, .parent_ids = {c1}, .author = sig, .committer = sig, .message = "c2\n"
    }).value();

    sig.timestamp = 1200;
    const Oid c3 = create_commit_in_odb(*repo, CreateCommitParams{
        .tree_id = empty_tree, .parent_ids = {c2}, .author = sig, .committer = sig, .message = "c3\n"
    }).value();

    // Ustawiamy gałąź main na c3
    git_reference* ref_main = nullptr;
    REQUIRE(git_reference_create(&ref_main, repo->handle(), "refs/heads/main", &c3.raw(), 1, nullptr) == 0);
    git_reference_free(ref_main);
    REQUIRE(git_repository_set_head(repo->handle(), "refs/heads/main") == 0);

    // Wczytujemy DagSnapshot
    auto dag = load_dag_snapshot(*repo);
    REQUIRE(dag.has_value());
    REQUIRE(dag->size() == 3);

    // Planujemy modyfikację commitu c1 (zmieniamy timestamp autora na 500)
    RewritePlanner planner(*dag);
    CommitSignature new_author = sig;
    new_author.timestamp = 500;
    planner.set_modify_action(c1, new_author);

    const auto cascade = planner.simulate_cascade();
    REQUIRE(cascade.at(c1) == CommitTransformType::Modify1to1);
    REQUIRE(cascade.at(c2) == CommitTransformType::CascadeShaOnly);
    REQUIRE(cascade.at(c3) == CommitTransformType::CascadeShaOnly);

    // Wykonujemy przepisanie przez silnik
    RewriteEngine engine(*repo, *dag);
    const auto report = engine.execute(planner);
    REQUIRE(report.has_value());
    REQUIRE(report->original_commit_count == 3);
    REQUIRE(report->rewritten_commit_count == 3);

    // Weryfikujemy, że gałąź main wskazuje teraz na nowy commit (inny niż c3)
    git_oid new_main_oid{};
    REQUIRE(git_reference_name_to_id(&new_main_oid, repo->handle(), "refs/heads/main") == 0);
    REQUIRE(Oid(new_main_oid) != c3);

    // Sprawdzamy atomowe przywrócenie (Undo) przez BackupManager
    BackupManager backup_mgr(*repo);
    REQUIRE(backup_mgr.restore_latest().has_value());

    git_oid restored_oid{};
    REQUIRE(git_reference_name_to_id(&restored_oid, repo->handle(), "refs/heads/main") == 0);
    REQUIRE(Oid(restored_oid) == c3); // Z powrotem dokładnie c3!
}

TEST_CASE("RewriteEngine: Rozbicie commitu 1 -> K (Split) i sprawdzenie stanu drzew", "[core][engine][integration]") {
    GitLibInit init;
    TempDir dir;

    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, dir.path().string().c_str(), 0) == 0);
    RepositoryPtr repo_owner(raw);

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    InMemorySplitter splitter(*repo);
    const Oid empty_tree = splitter.empty_tree_oid().value();

    CommitSignature sig{.name = "Dev", .email = "dev@example.com", .timestamp = 1000, .tz_offset_min = 60};

    // Commit 1 (korzeń z pustym drzewem)
    const Oid c1 = create_commit_in_odb(*repo, CreateCommitParams{
        .tree_id = empty_tree, .parent_ids = {}, .author = sig, .committer = sig, .message = "root\n"
    }).value();

    // Commit 2 dodaje 2 pliki: include/pool.hpp i src/pool.cpp
    const Oid b_hpp = create_blob_data(repo->handle(), "// pool header\n");
    const Oid b_cpp = create_blob_data(repo->handle(), "// pool cpp\n");
    const Oid tree_final = create_tree_two_files(repo->handle(), "include/pool.hpp", b_hpp, "src/pool.cpp", b_cpp);

    sig.timestamp = 2000;
    const Oid c2 = create_commit_in_odb(*repo, CreateCommitParams{
        .tree_id = tree_final, .parent_ids = {c1}, .author = sig, .committer = sig, .message = "feat: add pool module\n"
    }).value();

    git_reference* ref_main = nullptr;
    REQUIRE(git_reference_create(&ref_main, repo->handle(), "refs/heads/main", &c2.raw(), 1, nullptr) == 0);
    git_reference_free(ref_main);
    REQUIRE(git_repository_set_head(repo->handle(), "refs/heads/main") == 0);

    auto dag = load_dag_snapshot(*repo);
    REQUIRE(dag.has_value());

    // Pobieramy FileDelta dla c2
    const auto deltas = splitter.diff_trees(empty_tree, tree_final).value();
    REQUIRE(deltas.size() == 2);

    // Planujemy rozbicie na K = 2
    const auto split_plan = CommitSplitter::plan_file_split(*dag->find(c2), deltas, 2).value();

    RewritePlanner planner(*dag);
    planner.set_split_action(c2, split_plan);

    RewriteEngine engine(*repo, *dag);
    const auto report = engine.execute(planner);
    REQUIRE(report.has_value());

    // Łańcuch dla c2 powinien mieć teraz 2 nowe commity
    const auto& chain = report->chain_map.at(c2);
    REQUIRE(chain.size() == 2);

    // Ostatnie ogniwo nowego łańcucha musi mieć DOKŁADNIE identyczne drzewo jak c2 (Boundary Tree Invariant)
    git_commit* last_split_commit = nullptr;
    REQUIRE(git_commit_lookup(&last_split_commit, repo->handle(), &chain.back().raw()) == 0);
    CommitPtr last_owner(last_split_commit);
    REQUIRE(Oid(*git_commit_tree_id(last_split_commit)) == tree_final);

    // Pierwsze ogniwo nowego łańcucha powinno mieć drzewo pośrednie zawierające tylko 1 plik
    git_commit* first_split_commit = nullptr;
    REQUIRE(git_commit_lookup(&first_split_commit, repo->handle(), &chain.front().raw()) == 0);
    CommitPtr first_owner(first_split_commit);
    git_tree* intermediate_tree = nullptr;
    REQUIRE(git_commit_tree(&intermediate_tree, first_split_commit) == 0);
    TreePtr inter_owner(intermediate_tree);
    REQUIRE(git_tree_entrycount(intermediate_tree) == 1);

    // W folderze roboczym na dysku nie powstały żadne pliki (Zero Working-Tree Touch)
    REQUIRE_FALSE(fs::exists(dir.path() / "include"));
    REQUIRE_FALSE(fs::exists(dir.path() / "src"));
}

