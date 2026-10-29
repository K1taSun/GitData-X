#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "core/backup_manager.hpp"
#include "git/error.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::core;
using namespace gitdatax::git;
using gitdatax::testing::TempDir;

namespace {

// Pomocnicza klasa do przygotowania repozytorium testowego
class GitRepoFixture {
public:
    explicit GitRepoFixture(const fs::path& path) {
        git_repository* raw = nullptr;
        REQUIRE(git_repository_init(&raw, path.string().c_str(), 0) == 0);
        repo_.reset(raw);

        git_treebuilder* tb = nullptr;
        REQUIRE(git_treebuilder_new(&tb, repo_.get(), nullptr) == 0);
        REQUIRE(git_treebuilder_write(&tree_id_, tb) == 0);
        git_treebuilder_free(tb);
    }

    Oid create_commit(const std::string& msg, const std::vector<Oid>& parents = {}) {
        git_signature* sig = nullptr;
        REQUIRE(git_signature_new(&sig, "Tester", "test@test.com", 1000, 0) == 0);
        SignaturePtr sig_ptr(sig);

        std::vector<const git_oid*> raw_parents;
        for (const auto& p : parents) raw_parents.push_back(&p.raw());

        git_oid created{};
        REQUIRE(git_commit_create_from_ids(&created, repo_.get(), nullptr, sig_ptr.get(),
                                           sig_ptr.get(), nullptr, msg.c_str(), &tree_id_,
                                           raw_parents.size(), raw_parents.data()) == 0);
        return Oid(created);
    }

    void create_branch(const std::string& name, const Oid& target) {
        git_reference* raw = nullptr;
        REQUIRE(git_reference_create(&raw, repo_.get(), ("refs/heads/" + name).c_str(),
                                     &target.raw(), 1, nullptr) == 0);
        ReferencePtr owner(raw);
    }

    void set_head(const std::string& branch_ref) {
        REQUIRE(git_repository_set_head(repo_.get(), branch_ref.c_str()) == 0);
    }

    void set_head_detached(const Oid& target) {
        REQUIRE(git_repository_set_head_detached(repo_.get(), &target.raw()) == 0);
    }

private:
    RepositoryPtr repo_;
    git_oid tree_id_{};
};

}  // namespace

TEST_CASE("BackupManager: Tworzenie i atomowe przywracanie stanu (Undo)", "[core][backup]") {
    GitLibInit init;
    TempDir dir;

    Oid c1, c2, c3;
    {
        GitRepoFixture fixture(dir.path());
        c1 = fixture.create_commit("commit 1");
        c2 = fixture.create_commit("commit 2", {c1});
        c3 = fixture.create_commit("commit 3", {c2});

        fixture.create_branch("main", c2);
        fixture.create_branch("feature", c3);
        fixture.set_head("refs/heads/main");
    }

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    BackupManager manager(*repo);

    // 1. Tworzymy punkt kontrolny przed modyfikacją
    const auto chk = manager.create_checkpoint({"refs/heads/main", "refs/heads/feature"}, "checkpoint_1");
    REQUIRE(chk.has_value());
    REQUIRE(chk->id == "checkpoint_1");
    REQUIRE(chk->branches.size() == 2);
    REQUIRE_FALSE(chk->is_head_detached);
    REQUIRE(chk->head_symbolic_ref == "refs/heads/main");

    // 2. Modyfikujemy gałęzie (symulacja zapisu nowej historii)
    git_reference* ref_main = nullptr;
    REQUIRE(git_reference_create(&ref_main, repo->handle(), "refs/heads/main", &c1.raw(), 1, nullptr) == 0);
    git_reference_free(ref_main);

    git_reference* ref_feat = nullptr;
    REQUIRE(git_reference_create(&ref_feat, repo->handle(), "refs/heads/feature", &c1.raw(), 1, nullptr) == 0);
    git_reference_free(ref_feat);

    // Potwierdzamy, że gałęzie zostały zmienione
    git_oid resolved_main{};
    REQUIRE(git_reference_name_to_id(&resolved_main, repo->handle(), "refs/heads/main") == 0);
    REQUIRE(Oid(resolved_main) == c1);

    // 3. Sprawdzamy listowanie punktów kontrolnych
    const auto list = manager.list_checkpoints();
    REQUIRE(list.has_value());
    REQUIRE_FALSE(list->empty());
    REQUIRE(list->front().id == "checkpoint_1");

    // 4. Atomowe przywrócenie (Undo)
    const auto undo_res = manager.restore_latest();
    REQUIRE(undo_res.has_value());

    // 5. Potwierdzamy, że referencje wróciły do pierwotnego stanu
    REQUIRE(git_reference_name_to_id(&resolved_main, repo->handle(), "refs/heads/main") == 0);
    REQUIRE(Oid(resolved_main) == c2); // c2, nie c1!

    git_oid resolved_feat{};
    REQUIRE(git_reference_name_to_id(&resolved_feat, repo->handle(), "refs/heads/feature") == 0);
    REQUIRE(Oid(resolved_feat) == c3); // c3, nie c1!
}

TEST_CASE("BackupManager: Obsluga stanu Detached HEAD", "[core][backup]") {
    GitLibInit init;
    TempDir dir;
    Oid c1, c2;
    {
        GitRepoFixture fixture(dir.path());
        c1 = fixture.create_commit("commit 1");
        c2 = fixture.create_commit("commit 2", {c1});
        fixture.create_branch("main", c2);
        fixture.set_head_detached(c1);
    }

    auto repo = Repo::open_from_path(dir.path());
    REQUIRE(repo.has_value());

    BackupManager manager(*repo);
    const auto chk = manager.create_checkpoint({"refs/heads/main"}, "detached_chk");
    REQUIRE(chk.has_value());
    REQUIRE(chk->is_head_detached);
    REQUIRE(chk->head_target == c1);

    // Przestawiamy detached HEAD na c2
    REQUIRE(git_repository_set_head_detached(repo->handle(), &c2.raw()) == 0);

    // Przywracamy
    REQUIRE(manager.restore_checkpoint(*chk).has_value());

    // Sprawdzamy czy HEAD to znowu c1
    git_oid head_target{};
    REQUIRE(git_reference_name_to_id(&head_target, repo->handle(), "HEAD") == 0);
    REQUIRE(Oid(head_target) == c1);
}
