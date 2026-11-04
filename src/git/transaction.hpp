#pragma once

#include <optional>
#include <string>
#include <vector>

#include "git/error.hpp"
#include "git/oid.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"

namespace gitdatax::git {

// Reprezentacja aktualizacji pojedynczej gałęzi w trybie CAS (Compare-And-Swap).
struct BranchUpdate {
    std::string ref_name; // Pełna nazwa referencji, np. "refs/heads/main"
    Oid expected_old_oid; // Oryginalny OID - CAS chroni przed wyścigami zewnętrznymi
    Oid new_oid;          // Nowy końcowy OID po przepisaniu DAG
};

// Atomowa transakcja aktualizacji referencji za pomocą git_transaction (libgit2).
// Gwarantuje, że gałęzie zostaną zaktualizowane albo wszystkie, albo żadna,
// eliminując ryzyko częściowych zmian i uszkodzenia wskaźników repozytorium.
class RefTransaction {
public:
    explicit RefTransaction(Repo& repo);

    // Rejestruje aktualizację gałęzi z weryfikacją CAS.
    GitResult<void> add_branch_update(const BranchUpdate& update);

    // Jawna aktualizacja stanu HEAD (dla trybu Detached oraz Symbolic).
    void set_detached_head_target(const Oid& target);
    void set_symbolic_head_target(const std::string& branch_ref);

    // Atomowe zatwierdzenie transakcji CAS oraz synchronizacja HEAD.
    GitResult<void> commit();

private:
    Repo& repo_;
    TransactionPtr tx_;
    std::vector<BranchUpdate> updates_;
    std::optional<Oid> detached_head_target_{std::nullopt};
    std::optional<std::string> symbolic_head_target_{std::nullopt};
};

}  // namespace gitdatax::git
