#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "git/error.hpp"
#include "git/oid.hpp"
#include "git/repo.hpp"

namespace gitdatax::core {

struct BackupEntry {
    std::string ref_name; // Pełna nazwa referencji, np. "refs/heads/main"
    git::Oid target_oid;   // Docelowy hash OID przed zmianą
};

struct BackupCheckpoint {
    std::string id;       // Znacznik czasowy punktu kontrolnego (np. "20261111_020900")
    std::vector<BackupEntry> branches;
    git::Oid head_target;
    bool is_head_detached{false};
    std::string head_symbolic_ref; // Np. "refs/heads/main" jeśli HEAD był symboliczny
};

// Moduł zarządzania kopiami zapasowymi i atomowym Undo (Sekcja 8.3).
// Zapisuje referencje pod refs/gitdatax/backup/<id>/...
// Zapewnia ochronę przed git gc oraz jedno-klikowe wycofanie zmian [u].
class BackupManager {
public:
    explicit BackupManager(git::Repo& repo);

    // Tworzy punkt kontrolny refs/gitdatax/backup/<timestamp>/...
    // Zapisuje OID wszystkich gałęzi podanych w branch_ref_names oraz stan HEAD.
    git::GitResult<BackupCheckpoint> create_checkpoint(
        const std::vector<std::string>& branch_ref_names,
        std::string custom_id = ""
    );

    // Zwraca listę wszystkich dostępnych punktów kontrolnych od najnowszego do najstarszego.
    git::GitResult<std::vector<BackupCheckpoint>> list_checkpoints();

    // Przywraca stan repozytorium do stanu zapisanego w danym punkcie kontrolnym (Undo).
    git::GitResult<void> restore_checkpoint(const BackupCheckpoint& checkpoint);

    // Przywraca stan z najnowszego dostępnego punktu kontrolnego.
    git::GitResult<void> restore_latest();

private:
    git::Repo& repo_;
};

}  // namespace gitdatax::core
