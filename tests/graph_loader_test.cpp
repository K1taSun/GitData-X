#include <catch2/catch_test_macros.hpp>

#include <git2.h>
#include <git2/sys/commit.h>

#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/dag_snapshot.hpp"
#include "git/graph_loader.hpp"
#include "git/oid.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "graph_fixture.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::git;
using gitdatax::core::DagSnapshot;
using gitdatax::core::HeadKind;
using gitdatax::testing::every_parent_before_child;
using gitdatax::testing::TempDir;

namespace {

// Buduje repozytorium z pustymi drzewami; liczy się tylko kształt grafu.
class TestRepo {
public:
    explicit TestRepo(const fs::path& path) {
        git_repository* raw = nullptr;
        REQUIRE(git_repository_init(&raw, path.string().c_str(), 0) == 0);
        repo_.reset(raw);

        git_treebuilder* builder = nullptr;
        REQUIRE(git_treebuilder_new(&builder, repo_.get(), nullptr) == 0);
        const int code = git_treebuilder_write(&empty_tree_, builder);
        git_treebuilder_free(builder);
        REQUIRE(code == 0);
    }

    git_oid commit(const std::string& message, const std::vector<git_oid>& parents, git_time_t time) {
        git_signature* raw_signature = nullptr;
        REQUIRE(git_signature_new(&raw_signature, "Jan Kowalski", "jan@example.com", time, 60) == 0);
        SignaturePtr signature(raw_signature);

        // libgit2 oczekuje tablicy wskaźników na rodziców.
        std::vector<const git_oid*> parent_pointers;
        for (const git_oid& parent : parents) {
            parent_pointers.push_back(&parent);
        }

        git_oid created{};
        REQUIRE(git_commit_create_from_ids(&created, repo_.get(), nullptr, signature.get(),
                                           signature.get(), nullptr, message.c_str(), &empty_tree_,
                                           parent_pointers.size(), parent_pointers.data()) == 0);
        return created;
    }

    void branch(const std::string& name, const git_oid& target) {
        git_reference* raw = nullptr;
        REQUIRE(git_reference_create(&raw, repo_.get(), ("refs/heads/" + name).c_str(), &target, 1,
                                     nullptr) == 0);
        ReferencePtr owner(raw);
    }

    void lightweight_tag(const std::string& name, const git_oid& target) {
        git_reference* raw = nullptr;
        REQUIRE(git_reference_create(&raw, repo_.get(), ("refs/tags/" + name).c_str(), &target, 1,
                                     nullptr) == 0);
        ReferencePtr owner(raw);
    }

    void annotated_tag(const std::string& name, const git_oid& target) {
        git_object* raw_object = nullptr;
        REQUIRE(git_object_lookup(&raw_object, repo_.get(), &target, GIT_OBJECT_COMMIT) == 0);
        ObjectPtr object(raw_object);

        git_signature* raw_signature = nullptr;
        REQUIRE(git_signature_new(&raw_signature, "Tagger", "tagger@example.com", 1000, 0) == 0);
        SignaturePtr signature(raw_signature);

        git_oid tag_id{};
        REQUIRE(git_tag_create(&tag_id, repo_.get(), name.c_str(), object.get(), signature.get(),
                               "adnotowany tag\n", 1) == 0);
    }

    void set_head_branch(const std::string& name) {
        REQUIRE(git_repository_set_head(repo_.get(), ("refs/heads/" + name).c_str()) == 0);
    }

    void set_head_detached(const git_oid& target) {
        REQUIRE(git_repository_set_head_detached(repo_.get(), &target) == 0);
    }

    const git_oid& empty_tree() const { return empty_tree_; }

private:
    RepositoryPtr repo_;
    git_oid empty_tree_{};
};

// Zwraca zrzut po zamknięciu wszystkich uchwytów libgit2 użytych do wczytania.
DagSnapshot load_and_close(const fs::path& path) {
    auto repo = Repo::open_from_path(path);
    REQUIRE(repo.has_value());
    auto snapshot = load_dag_snapshot(*repo);
    REQUIRE(snapshot.has_value());
    return std::move(*snapshot);
}

}  // namespace

TEST_CASE("Loader wczytuje graf z 3 merge, galeziami, tagami i HEAD", "[git][loader][integration]") {
    GitLibInit init;
    TempDir dir;

    git_oid root, a1, b1, c1, m1, m2, f1, f2, m3;
    {
        TestRepo repo(dir.path());
        root = repo.commit("root\n", {}, 100);
        a1 = repo.commit("a1\n", {root}, 110);
        b1 = repo.commit("b1\n", {root}, 115);
        c1 = repo.commit("c1\n", {root}, 120);
        m1 = repo.commit("merge 1\n", {a1, b1}, 200);
        m2 = repo.commit("merge 2\n", {m1, c1}, 210);
        f1 = repo.commit("f1\n", {root}, 130);
        f2 = repo.commit("f2\n", {f1}, 140);
        m3 = repo.commit("Merge trzech\n\nTresc scalenia\n", {m2, f2, c1}, 300);

        repo.branch("main", m3);
        repo.branch("feature/a", a1);
        repo.branch("feature/f", f2);
        repo.lightweight_tag("v1", root);
        repo.annotated_tag("v2", m1);
        repo.set_head_branch("main");
    }

    const DagSnapshot snapshot = load_and_close(dir.path());

    REQUIRE(snapshot.size() == 9);

    REQUIRE(snapshot.branches().size() == 3);
    REQUIRE(snapshot.branches()[0].name == "refs/heads/feature/a");
    REQUIRE(snapshot.branches()[1].name == "refs/heads/feature/f");
    REQUIRE(snapshot.branches()[2].name == "refs/heads/main");
    REQUIRE(snapshot.branches()[2].target == Oid(m3));

    REQUIRE(snapshot.tags().size() == 2);
    REQUIRE(snapshot.tags()[0].name == "refs/tags/v1");
    REQUIRE(snapshot.tags()[0].target == Oid(root));
    // Tag adnotowany ma wskazywać commit, a nie obiekt tagu.
    REQUIRE(snapshot.tags()[1].name == "refs/tags/v2");
    REQUIRE(snapshot.tags()[1].target == Oid(m1));

    REQUIRE(snapshot.head().kind == HeadKind::Symbolic);
    REQUIRE(snapshot.head().branch_ref == "refs/heads/main");
    REQUIRE(snapshot.head().target == Oid(m3));

    const auto* merge = snapshot.find(Oid(m3));
    REQUIRE(merge != nullptr);
    REQUIRE(merge->is_merge());
    REQUIRE(merge->parents == std::vector<Oid>{Oid(m2), Oid(f2), Oid(c1)});
    REQUIRE(merge->summary == "Merge trzech");
    REQUIRE(merge->body == "Tresc scalenia");
    REQUIRE(merge->author.name == "Jan Kowalski");
    REQUIRE(merge->author.email == "jan@example.com");
    REQUIRE(merge->author.timestamp == 300);
    REQUIRE(merge->author.tz_offset_min == 60);
    REQUIRE(merge->committer.timestamp == 300);
    REQUIRE_FALSE(merge->has_gpgsig);
    REQUIRE_FALSE(merge->mergetag.has_value());
    REQUIRE_FALSE(merge->message_encoding.has_value());

    const auto* root_node = snapshot.find(Oid(root));
    REQUIRE(root_node != nullptr);
    REQUIRE(root_node->is_root());
    REQUIRE(root_node->tree_id == merge->tree_id);

    const auto sorted = snapshot.topological_sort_oldest_first();
    REQUIRE(sorted.has_value());
    REQUIRE(every_parent_before_child(snapshot, *sorted));
    REQUIRE(sorted->front() == Oid(root));
    REQUIRE(sorted->back() == Oid(m3));
}

TEST_CASE("Zrzut mozna czytac z wielu watkow po zamknieciu repozytorium", "[git][loader][threads]") {
    GitLibInit init;
    TempDir dir;
    git_oid last;
    {
        TestRepo repo(dir.path());
        git_oid previous = repo.commit("c0\n", {}, 100);
        for (int i = 1; i < 30; ++i) {
            previous = repo.commit("c" + std::to_string(i) + "\n", {previous}, 100 + i);
        }
        last = previous;
        repo.branch("main", last);
        repo.set_head_branch("main");
    }
    const DagSnapshot snapshot = load_and_close(dir.path());
    const auto reference = snapshot.topological_sort_oldest_first().value();

    // Repozytorium jest już zamknięte, a katalog usunięty zaraz po teście; wątki widzą tylko RAM.
    constexpr int kThreads = 8;
    std::vector<int> failures(kThreads, 0);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&snapshot, &reference, &failures, &last, t]() {
            for (int round = 0; round < 100; ++round) {
                const auto sorted = snapshot.topological_sort_oldest_first();
                if (!sorted.has_value() || *sorted != reference ||
                    snapshot.find(Oid(last)) == nullptr) {
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

TEST_CASE("Loader rozpoznaje Detached HEAD", "[git][loader][integration]") {
    GitLibInit init;
    TempDir dir;
    git_oid first, second;
    {
        TestRepo repo(dir.path());
        first = repo.commit("first\n", {}, 100);
        second = repo.commit("second\n", {first}, 200);
        repo.branch("main", second);
        repo.set_head_detached(first);
    }

    const DagSnapshot snapshot = load_and_close(dir.path());

    REQUIRE(snapshot.head().kind == HeadKind::Detached);
    REQUIRE(snapshot.head().target == Oid(first));
    REQUIRE(snapshot.head().branch_ref.empty());
    REQUIRE(snapshot.size() == 2);
}

TEST_CASE("Loader obsluguje repozytorium bez commitow", "[git][loader][integration]") {
    GitLibInit init;
    TempDir dir;
    { TestRepo repo(dir.path()); }

    const DagSnapshot snapshot = load_and_close(dir.path());

    REQUIRE(snapshot.empty());
    REQUIRE(snapshot.head().kind == HeadKind::Unborn);
    REQUIRE_FALSE(snapshot.head().target.has_value());
    REQUIRE(snapshot.head().branch_ref.starts_with("refs/heads/"));
    REQUIRE(snapshot.branches().empty());
    REQUIRE(snapshot.tags().empty());
}

