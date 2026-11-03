#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/dag_snapshot.hpp"
#include "core/rewrite_planner.hpp"
#include "git/error.hpp"
#include "git/oid.hpp"
#include "git/repo.hpp"

namespace gitdatax::core {

// Raport z pomyślnie wykonanego przepisania historii
struct RewriteExecutionReport {
    std::size_t original_commit_count{0};
    std::size_t rewritten_commit_count{0};
    std::unordered_map<git::Oid, std::vector<git::Oid>> chain_map;
    std::vector<std::string> updated_branches;
    std::string backup_checkpoint_id;
};

// Główny silnik wykonawczy bezdotykowego przepisywania grafu w pamięci RAM i ODB (Sekcja 3.2 i 5.2).
// Łączy w jedną atomową całość: BackupManager, InMemorySplitter, CommitWriter,
// DagVerifier oraz RefTransaction (CAS).
class RewriteEngine {
public:
    RewriteEngine(git::Repo& repo, const DagSnapshot& dag);

    git::GitResult<RewriteExecutionReport> execute(const RewritePlanner& planner);

private:
    git::Repo& repo_;
    const DagSnapshot& dag_;
};

}  // namespace gitdatax::core
