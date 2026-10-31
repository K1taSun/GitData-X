#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/commit_node.hpp"
#include "git/error.hpp"
#include "git/oid.hpp"
#include "git/repo.hpp"

namespace gitdatax::git {

// Parametry niezbędne do utworzenia nowego obiektu commitu w bazie obiektów ODB.
struct CreateCommitParams {
    Oid tree_id;
    std::vector<Oid> parent_ids;
    core::CommitSignature author;
    core::CommitSignature committer;
    std::string message;
    std::optional<std::string> message_encoding{std::nullopt};
};

// Tworzy nowy commit bezpośrednio w bazie obiektów Gita (ODB) za pomocą git_commit_create_from_ids.
// Zero Working-Tree Touch: operacja odbywa się całkowicie w pamięci RAM i bazie obiektów,
// bez dotykania plików na dysku, bez przełączania HEAD i bez zmian w .git/index.
GitResult<Oid> create_commit_in_odb(
    const Repo& repo,
    const CreateCommitParams& params
);

}  // namespace gitdatax::git
