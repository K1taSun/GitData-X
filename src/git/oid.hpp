#pragma once

#include <git2.h>

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "git/error.hpp"

namespace gitdatax::git {

// Silnie typowany identyfikator obiektu Gita (SHA-1, 20 bajtów).
// Domyślnie to "zerowy" Oid; służy jako wartość pusta.
class Oid {
public:
    static constexpr std::size_t kHexLength = 40;

    Oid() = default;
    explicit Oid(const git_oid& raw) : raw_(raw) {}

    // GitResult zamiast wyjątku: zły tekst od użytkownika to zwykła sytuacja, nie wyjątek.
    static GitResult<Oid> from_hex(std::string_view hex);

    std::string to_hex() const;
    bool is_zero() const;

    // Dostęp do surowej struktury, gdy trzeba wywołać funkcję C z libgit2.
    const git_oid& raw() const { return raw_; }

    friend bool operator==(const Oid& lhs, const Oid& rhs);
    friend std::strong_ordering operator<=>(const Oid& lhs, const Oid& rhs);

private:
    git_oid raw_{};
};

}  // namespace gitdatax::git

// Specjalizacja umożliwia użycie Oid jako klucza w std::unordered_map.
template <>
struct std::hash<gitdatax::git::Oid> {
    std::size_t operator()(const gitdatax::git::Oid& oid) const noexcept;
};
