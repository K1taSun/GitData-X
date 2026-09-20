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

// RAII dla globalnego stanu libgit2: init w konstruktorze, shutdown w destruktorze.
// libgit2 liczy referencje, więc zagnieżdżone instancje są bezpieczne.
// Kopiowanie jest zablokowane, bo każda kopia wywołałaby shutdown drugi raz.
class GitLibInit {
public:
    // Rzuca std::runtime_error, bo bez zainicjowanej biblioteki dalsza praca nie ma sensu.
    GitLibInit();
    ~GitLibInit();

    GitLibInit(const GitLibInit&) = delete;
    GitLibInit& operator=(const GitLibInit&) = delete;
};

}  // namespace gitdatax::git
