#include "git/transaction.hpp"

#include <git2.h>

namespace gitdatax::git {

RefTransaction::RefTransaction(Repo& repo) : repo_(repo) {
    git_transaction* raw = nullptr;
    const int code = git_transaction_new(&raw, repo_.handle());
    if (code == 0) {
        tx_.reset(raw);
    }
}

GitResult<void> RefTransaction::add_branch_update(const BranchUpdate& update) {
    if (!tx_) {
        return std::unexpected(GitError{GIT_ERROR, "Nie udalo sie utworzyc obiektu git_transaction"});
    }

    // Weryfikacja CAS (Compare-And-Swap): sprawdzamy czy gałąź nie zmieniła się w tle
    git_reference* raw_ref = nullptr;
    const int lookup_code = git_reference_lookup(&raw_ref, repo_.handle(), update.ref_name.c_str());
    if (lookup_code < 0) {
        return std::unexpected(make_git_error(lookup_code));
    }
    ReferencePtr ref(raw_ref);

    git_object* raw_commit = nullptr;
    const int peel_code = git_reference_peel(&raw_commit, ref.get(), GIT_OBJECT_COMMIT);
    if (peel_code < 0) {
        return std::unexpected(make_git_error(peel_code));
    }
    ObjectPtr commit_obj(raw_commit);
    const Oid actual_current_oid(*git_object_id(commit_obj.get()));

    if (!update.expected_old_oid.is_zero() && actual_current_oid != update.expected_old_oid) {
        return std::unexpected(GitError{
            GIT_EMODIFIED,
            "Konflikt CAS: galaz " + update.ref_name + " zostala zmodyfikowana w tle (oczekiwano " +
            update.expected_old_oid.to_hex() + ", aktualnie " + actual_current_oid.to_hex() + ")"
        });
    }

    // Blokujemy referencję w transakcji
    const int lock_code = git_transaction_lock_ref(tx_.get(), update.ref_name.c_str());
    if (lock_code < 0) {
        return std::unexpected(make_git_error(lock_code));
    }

    // Rejestrujemy nowy cel w transakcji
    const int set_code = git_transaction_set_target(
        tx_.get(),
        update.ref_name.c_str(),
        &update.new_oid.raw(),
        nullptr,
        "gitdatax: rewrite branches"
    );
    if (set_code < 0) {
        return std::unexpected(make_git_error(set_code));
    }

    updates_.push_back(update);
    return {};
}

void RefTransaction::set_detached_head_target(const Oid& target) {
    detached_head_target_ = target;
    symbolic_head_target_ = std::nullopt;
}

void RefTransaction::set_symbolic_head_target(const std::string& branch_ref) {
    symbolic_head_target_ = branch_ref;
    detached_head_target_ = std::nullopt;
}

GitResult<void> RefTransaction::commit() {
    if (!tx_) {
        return std::unexpected(GitError{GIT_ERROR, "Brak aktywnej transakcji"});
    }

    // 1. Atomowe zatwierdzenie zmian we wszystkich zablokowanych referencjach gałęzi
    const int commit_code = git_transaction_commit(tx_.get());
    if (commit_code < 0) {
        return std::unexpected(make_git_error(commit_code));
    }

    // 2. Jawna aktualizacja stanu HEAD (Sekcja 2, Ryzyko 1: Spójność HEAD w trybie Detached)
    if (detached_head_target_.has_value()) {
        const int head_code = git_repository_set_head_detached(
            repo_.handle(),
            &detached_head_target_->raw()
        );
        if (head_code < 0) {
            return std::unexpected(make_git_error(head_code));
        }
    } else if (symbolic_head_target_.has_value()) {
        const int head_code = git_repository_set_head(
            repo_.handle(),
            symbolic_head_target_->c_str()
        );
        if (head_code < 0) {
            return std::unexpected(make_git_error(head_code));
        }
    }

    return {};
}

}  // namespace gitdatax::git
