#pragma once

#include <expected>
#include <string>

namespace gitdatax::git {

// Błąd libgit2: kod zwrotny + tekst z git_error_last().
struct GitError {
    int code{0};
    std::string message;
};

// std::expected zamiast wyjątków: błąd jest częścią typu zwracanego,
// więc kompilator nie pozwala go przypadkiem zignorować.
template <typename T>
using GitResult = std::expected<T, GitError>;

// Buduje GitError z kodu zwrotnego i ostatniego komunikatu libgit2.
GitError make_git_error(int code);

// Zamienia kod zwrotny libgit2 (< 0 oznacza błąd) na GitResult<void>.
GitResult<void> check_git_call(int code);

}  // namespace gitdatax::git
