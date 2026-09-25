#pragma once

#include <git2.h>

#include <memory>

namespace gitdatax::git {

// Każdy deleter to osobna struktura wywołująca odpowiednią funkcję *_free z libgit2.
// Dzięki temu std::unique_ptr zwalnia zasób sam, także przy wyjątku lub wcześniejszym return.
// Zwykłe struktury zamiast szablonu z wskaźnikiem na funkcję: prostsze do debugowania
// i działają z dynamicznie linkowaną libgit2 na Windows.

struct RepositoryDeleter {
    void operator()(git_repository* ptr) const noexcept { git_repository_free(ptr); }
};
struct CommitDeleter {
    void operator()(git_commit* ptr) const noexcept { git_commit_free(ptr); }
};
struct TreeDeleter {
    void operator()(git_tree* ptr) const noexcept { git_tree_free(ptr); }
};
struct IndexDeleter {
    void operator()(git_index* ptr) const noexcept { git_index_free(ptr); }
};
struct DiffDeleter {
    void operator()(git_diff* ptr) const noexcept { git_diff_free(ptr); }
};
struct PatchDeleter {
    void operator()(git_patch* ptr) const noexcept { git_patch_free(ptr); }
};
struct RevwalkDeleter {
    void operator()(git_revwalk* ptr) const noexcept { git_revwalk_free(ptr); }
};
struct SignatureDeleter {
    void operator()(git_signature* ptr) const noexcept { git_signature_free(ptr); }
};
struct TransactionDeleter {
    void operator()(git_transaction* ptr) const noexcept { git_transaction_free(ptr); }
};
struct ReferenceDeleter {
    void operator()(git_reference* ptr) const noexcept { git_reference_free(ptr); }
};

// unique_ptr (a nie shared_ptr): każdy uchwyt ma jednego właściciela, bez kosztu licznika.
using RepositoryPtr = std::unique_ptr<git_repository, RepositoryDeleter>;
using CommitPtr = std::unique_ptr<git_commit, CommitDeleter>;
using TreePtr = std::unique_ptr<git_tree, TreeDeleter>;
using IndexPtr = std::unique_ptr<git_index, IndexDeleter>;
using DiffPtr = std::unique_ptr<git_diff, DiffDeleter>;
using PatchPtr = std::unique_ptr<git_patch, PatchDeleter>;
using RevwalkPtr = std::unique_ptr<git_revwalk, RevwalkDeleter>;
using SignaturePtr = std::unique_ptr<git_signature, SignatureDeleter>;
using TransactionPtr = std::unique_ptr<git_transaction, TransactionDeleter>;
using ReferencePtr = std::unique_ptr<git_reference, ReferenceDeleter>;

}  // namespace gitdatax::git
