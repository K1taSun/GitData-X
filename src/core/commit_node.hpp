#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "git/oid.hpp"

namespace gitdatax::core {

// Kopia sygnatury Gita w pamięci RAM. Zwykłe stringi zamiast git_signature*,
// dzięki czemu wątek UI nie dotyka żadnych wskaźników libgit2.
struct CommitSignature {
    std::string name;
    std::string email;
    std::int64_t timestamp{0};  // sekundy od epoki Unix (UTC)
    int tz_offset_min{0};       // przesunięcie strefy w minutach względem UTC
};

// Pełna kopia commitu potrzebna do przepisywania historii.
struct CommitNode {
    git::Oid id;
    git::Oid tree_id;
    // Kolejność rodziców ma znaczenie (git log --first-parent, merge ośmiornicowy).
    std::vector<git::Oid> parents;

    CommitSignature author;
    CommitSignature committer;

    std::string summary;
    std::string body;

    bool has_gpgsig{false};
    // Surowy nagłówek mergetag; std::optional rozróżnia "brak" od "pusty".
    std::optional<std::string> mergetag;
    // Nagłówek "encoding"; brak oznacza domyślne UTF-8.
    std::optional<std::string> message_encoding;

    bool is_merge() const { return parents.size() > 1; }
    bool is_root() const { return parents.empty(); }
};

}  // namespace gitdatax::core

