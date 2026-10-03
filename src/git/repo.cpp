#include "git/repo.hpp"

#include <git2.h>

#include <string>
#include <utility>

namespace gitdatax::git {

namespace {

// libgit2 oczekuje ścieżek w UTF-8, a path::string() na Windows używa lokalnej strony kodowej.
std::string to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::filesystem::path from_utf8(const char* text) {
    const std::string utf8(text);
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

RepoState to_repo_state(int libgit_state) {
    switch (libgit_state) {
        case GIT_REPOSITORY_STATE_NONE:
            return RepoState::None;
        case GIT_REPOSITORY_STATE_MERGE:
            return RepoState::Merge;
        case GIT_REPOSITORY_STATE_REVERT:
        case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
            return RepoState::Revert;
        case GIT_REPOSITORY_STATE_CHERRYPICK:
        case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
            return RepoState::CherryPick;
        case GIT_REPOSITORY_STATE_BISECT:
            return RepoState::Bisect;
        case GIT_REPOSITORY_STATE_REBASE:
        case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
        case GIT_REPOSITORY_STATE_REBASE_MERGE:
            return RepoState::Rebase;
        default:
            return RepoState::ApplyMailbox;
    }
}

}  // namespace

Repo::Repo(RepositoryPtr repo) : repo_(std::move(repo)) {
    state_ = to_repo_state(git_repository_state(repo_.get()));
    // Przy rebase, merge czy bisect zapis mógłby zepsuć trwającą operację użytkownika.
    is_read_only_ = (state_ != RepoState::None);
    is_bare_ = git_repository_is_bare(repo_.get()) != 0;
    is_worktree_ = git_repository_is_worktree(repo_.get()) != 0;
}

GitResult<Repo> Repo::open_from_cwd() {
    return open_with_flags(".", GIT_REPOSITORY_OPEN_FROM_ENV);
}

GitResult<Repo> Repo::open_from_path(const std::filesystem::path& path) {
    return open_with_flags(to_utf8(path), 0);
}

std::filesystem::path Repo::git_dir() const {
    return from_utf8(git_repository_path(repo_.get()));
}

std::optional<std::filesystem::path> Repo::workdir() const {
    const char* dir = git_repository_workdir(repo_.get());
    if (dir == nullptr) {
        return std::nullopt;
    }
    return from_utf8(dir);
}

GitResult<Repo> Repo::open_with_flags(const std::string& path, unsigned int flags) {
    git_repository* raw = nullptr;
    const int code = git_repository_open_ext(&raw, path.c_str(), flags, nullptr);
    if (code < 0) {
        return std::unexpected(make_git_error(code));
    }
    // Od tej linii uchwyt ma właściciela, więc nie wycieknie nawet przy wyjątku.
    return Repo(RepositoryPtr(raw));
}

}  // namespace gitdatax::git
