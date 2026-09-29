#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/commit_node.hpp"
#include "git/oid.hpp"

namespace gitdatax::core {

// Gałąź lokalna (refs/heads/*) albo tag (refs/tags/*) wskazujący commit.
struct RefInfo {
    std::string name;  // pełna nazwa, np. "refs/heads/main"
    git::Oid target;   // dla tagów adnotowanych: commit po "obraniu" tagu
};

enum class HeadKind {
    Unborn,    // gałąź bez commitów (świeże repozytorium)
    Symbolic,  // HEAD wskazuje gałąź
    Detached,  // HEAD wskazuje bezpośrednio commit
};

struct HeadInfo {
    HeadKind kind{HeadKind::Unborn};
    std::string branch_ref;       // tylko dla Symbolic i Unborn
    std::optional<git::Oid> target;  // brak dla Unborn
};

// Niemutowalny zrzut grafu commitów w RAM.
// Po konstrukcji nic się nie zmienia, a wszystkie metody są const i nie mają
// mutable ani cache'u, więc wielu wątków może czytać obiekt jednocześnie bez muteksów.
// Obiekt nie trzyma żadnych wskaźników libgit2 - repozytorium można zamknąć.
class DagSnapshot {
public:
    // Parametry przez wartość + std::move: wywołujący może przekazać dane
    // bez kopiowania (std::move) albo świadomie skopiować.
    // Rzuca std::invalid_argument, gdy dwa commity mają ten sam Oid.
    DagSnapshot(std::vector<CommitNode> commits, std::vector<RefInfo> branches,
                std::vector<RefInfo> tags, HeadInfo head);

    std::size_t size() const { return commits_.size(); }
    bool empty() const { return commits_.empty(); }

    bool contains(const git::Oid& id) const;

    // Zwraca nullptr dla nieznanego Oid. Wskaźnik jest ważny tak długo, jak żyje zrzut.
    const CommitNode* find(const git::Oid& id) const;

    // Commity w kolejności wczytania (NIE topologicznej).
    const std::vector<CommitNode>& commits() const { return commits_; }
    const std::vector<RefInfo>& branches() const { return branches_; }
    const std::vector<RefInfo>& tags() const { return tags_; }
    const HeadInfo& head() const { return head_; }

    // Dzieci commitu (commity, które go wymieniają jako rodzica). Pusta lista dla nieznanego Oid.
    std::vector<git::Oid> children_of(const git::Oid& id) const;

private:
    std::vector<CommitNode> commits_;
    std::vector<RefInfo> branches_;
    std::vector<RefInfo> tags_;
    HeadInfo head_;

    // Indeksy zamiast kopii Oid: szybkie wyszukiwanie i mniejsze zużycie pamięci.
    std::unordered_map<git::Oid, std::size_t> index_by_id_;
    std::vector<std::vector<std::size_t>> children_;
};

}  // namespace gitdatax::core
