#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "git/error.hpp"
#include "git/raii.hpp"

namespace gitdatax::git {

// Uproszczony stan operacji w toku; wszystko poza None blokuje zapis.
enum class RepoState {
    None,
    Merge,
    Revert,
    CherryPick,
    Bisect,
    Rebase,
    ApplyMailbox,
};

// Otwarte repozytorium. Klasa jest tylko do przenoszenia (jeden właściciel uchwytu).
// UWAGA: git_repository nie jest bezpieczne wątkowo - używaj Repo wyłącznie
// z jednego wątku silnika ODB.
class Repo {
public:
    // Szuka repozytorium od bieżącego katalogu w górę i respektuje GIT_DIR
    // oraz GIT_CEILING_DIRECTORIES (flaga GIT_REPOSITORY_OPEN_FROM_ENV).
    static GitResult<Repo> open_from_cwd();

    // Szuka repozytorium od podanej ścieżki w górę. Zmienne środowiskowe są
    // ignorowane, bo jawna ścieżka ma pierwszeństwo.
    // Obsługuje repozytoria zwykłe, bare, worktree i submoduły (plik .git z gitlink).
    static GitResult<Repo> open_from_path(const std::filesystem::path& path);

    RepoState state() const { return state_; }
    bool is_read_only() const { return is_read_only_; }
    bool is_bare() const { return is_bare_; }
    bool is_worktree() const { return is_worktree_; }

    // Katalog .git (lub katalog repo bare); dla worktree jest to .git/worktrees/<nazwa>.
    std::filesystem::path git_dir() const;

    // Katalog roboczy; puste dla repozytoriów bare.
    std::optional<std::filesystem::path> workdir() const;

    // Surowy uchwyt dla niskopoziomowych wrapperów; Repo nadal jest właścicielem.
    git_repository* handle() const { return repo_.get(); }

private:
    explicit Repo(RepositoryPtr repo);

    static GitResult<Repo> open_with_flags(const std::string& path, unsigned int flags);

    RepositoryPtr repo_;
    RepoState state_{RepoState::None};
    bool is_read_only_{false};
    bool is_bare_{false};
    bool is_worktree_{false};
};

}  // namespace gitdatax::git
