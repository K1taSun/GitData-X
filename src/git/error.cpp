#include "git/error.hpp"

#include <git2.h>

#include <utility>

namespace gitdatax::git {

GitError make_git_error(int code) {
    // git_error_last() może zwrócić nullptr, gdy libgit2 nie zapisało komunikatu.
    const git_error* last = git_error_last();
    std::string message = (last != nullptr && last->message != nullptr)
                              ? last->message
                              : "nieznany blad libgit2";
    return GitError{code, std::move(message)};
}

GitResult<void> check_git_call(int code) {
    if (code < 0) {
        return std::unexpected(make_git_error(code));
    }
    return {};
}

}  // namespace gitdatax::git
