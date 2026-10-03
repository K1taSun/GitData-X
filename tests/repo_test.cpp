#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "git/error.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using namespace gitdatax::git;
using gitdatax::testing::TempDir;

namespace {

// Tworzy puste repozytorium przez libgit2 i od razu zwalnia uchwyt.
void init_repository(const fs::path& path, bool bare) {
    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, path.string().c_str(), bare ? 1 : 0) == 0);
    RepositoryPtr owner(raw);
}

void write_text_file(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file << text;
}

// Przywraca poprzedni katalog roboczy nawet gdy test padnie w środku.
class CwdGuard {
public:
    explicit CwdGuard(const fs::path& new_cwd) : previous_(fs::current_path()) {
        fs::current_path(new_cwd);
    }
    ~CwdGuard() {
        std::error_code ignored;
        fs::current_path(previous_, ignored);
    }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;

private:
    fs::path previous_;
};

}  // namespace

TEST_CASE("Repo otwiera sie z podkatalogu i ma stan NONE", "[git][repo][integration]") {
    GitLibInit init;
    TempDir dir;
    init_repository(dir.path(), false);
    const fs::path nested = dir.path() / "src" / "deep";
    fs::create_directories(nested);

    auto repo = Repo::open_from_path(nested);

    REQUIRE(repo.has_value());
    REQUIRE(repo->state() == RepoState::None);
    REQUIRE_FALSE(repo->is_read_only());
    REQUIRE_FALSE(repo->is_bare());
    REQUIRE(fs::equivalent(repo->git_dir(), dir.path() / ".git"));
    REQUIRE(repo->workdir().has_value());
    REQUIRE(fs::equivalent(*repo->workdir(), dir.path()));
}

TEST_CASE("Repo wykrywa stan MERGE i wlacza read-only", "[git][repo][integration]") {
    GitLibInit init;
    TempDir dir;
    init_repository(dir.path(), false);
    // libgit2 uznaje merge za trwający, gdy istnieje plik MERGE_HEAD.
    write_text_file(dir.path() / ".git" / "MERGE_HEAD",
                    "0123456789abcdef0123456789abcdef01234567\n");

    auto repo = Repo::open_from_path(dir.path());

    REQUIRE(repo.has_value());
    REQUIRE(repo->state() == RepoState::Merge);
    REQUIRE(repo->is_read_only());
}

TEST_CASE("Repo wykrywa stan REBASE i BISECT", "[git][repo][integration]") {
    GitLibInit init;
    {
        TempDir dir;
        init_repository(dir.path(), false);
        fs::create_directories(dir.path() / ".git" / "rebase-merge");
        write_text_file(dir.path() / ".git" / "rebase-merge" / "interactive", "");

        auto repo = Repo::open_from_path(dir.path());
        REQUIRE(repo.has_value());
        REQUIRE(repo->state() == RepoState::Rebase);
        REQUIRE(repo->is_read_only());
    }
    {
        TempDir dir;
        init_repository(dir.path(), false);
        write_text_file(dir.path() / ".git" / "BISECT_LOG", "");

        auto repo = Repo::open_from_path(dir.path());
        REQUIRE(repo.has_value());
        REQUIRE(repo->state() == RepoState::Bisect);
        REQUIRE(repo->is_read_only());
    }
}

TEST_CASE("Repo obsluguje repozytorium bare", "[git][repo][integration]") {
    GitLibInit init;
    TempDir dir;
    init_repository(dir.path(), true);

    auto repo = Repo::open_from_path(dir.path());

    REQUIRE(repo.has_value());
    REQUIRE(repo->is_bare());
    REQUIRE_FALSE(repo->workdir().has_value());
    REQUIRE(repo->state() == RepoState::None);
}

TEST_CASE("Repo obsluguje plik .git typu gitlink", "[git][repo][integration]") {
    // Tak samo wyglądają submoduły i worktree: .git jest plikiem wskazującym katalog z danymi.
    GitLibInit init;
    TempDir main_dir;
    TempDir linked_dir;
    init_repository(main_dir.path(), false);
    write_text_file(linked_dir.path() / ".git",
                    "gitdir: " + (main_dir.path() / ".git").generic_string() + "\n");

    auto repo = Repo::open_from_path(linked_dir.path());

    REQUIRE(repo.has_value());
    REQUIRE(fs::equivalent(repo->git_dir(), main_dir.path() / ".git"));
}

TEST_CASE("Repo zwraca blad poza repozytorium", "[git][repo][integration]") {
    GitLibInit init;
    TempDir dir;

    auto repo = Repo::open_from_path(dir.path() / "nie_istnieje");

    REQUIRE_FALSE(repo.has_value());
    REQUIRE(repo.error().code < 0);
    REQUIRE_FALSE(repo.error().message.empty());
}

TEST_CASE("Repo::open_from_cwd znajduje repozytorium w gore drzewa", "[git][repo][integration]") {
    GitLibInit init;
    TempDir dir;
    init_repository(dir.path(), false);
    const fs::path nested = dir.path() / "a" / "b";
    fs::create_directories(nested);
    CwdGuard guard(nested);

    auto repo = Repo::open_from_cwd();

    REQUIRE(repo.has_value());
    REQUIRE(fs::equivalent(repo->git_dir(), dir.path() / ".git"));
}
