#include "core/backup_manager.hpp"

#include <git2.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <utility>

#include "git/raii.hpp"

namespace gitdatax::core {

namespace {

std::string generate_timestamp_id() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    char buffer[64];
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_buf);
#endif
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d_%02d%02d%02d_%03d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, static_cast<int>(ms));
    return std::string(buffer);
}

}  // namespace

BackupManager::BackupManager(git::Repo& repo) : repo_(repo) {}

git::GitResult<BackupCheckpoint> BackupManager::create_checkpoint(
    const std::vector<std::string>& branch_ref_names,
    std::string custom_id) {

    const std::string checkpoint_id = custom_id.empty() ? generate_timestamp_id() : std::move(custom_id);
    const std::string backup_prefix = "refs/gitdatax/backup/" + checkpoint_id;

    BackupCheckpoint checkpoint;
    checkpoint.id = checkpoint_id;

    // 1. Zapisanie stanu HEAD
    const int detached = git_repository_head_detached(repo_.handle());
    if (detached < 0) {
        return std::unexpected(git::make_git_error(detached));
    }
    checkpoint.is_head_detached = (detached == 1);

    git_reference* raw_head = nullptr;
    const int head_code = git_reference_lookup(&raw_head, repo_.handle(), "HEAD");
    if (head_code < 0) {
        return std::unexpected(git::make_git_error(head_code));
    }
    git::ReferencePtr head_ref(raw_head);

    if (checkpoint.is_head_detached) {
        const git_oid* head_target_oid = git_reference_target(head_ref.get());
        if (head_target_oid != nullptr) {
            checkpoint.head_target = git::Oid(*head_target_oid);
            git_reference* created = nullptr;
            const std::string head_backup_ref = backup_prefix + "/HEAD";
            const int code = git_reference_create(&created, repo_.handle(), head_backup_ref.c_str(),
                                                  head_target_oid, 1, "gitdatax backup HEAD");
            if (code < 0) return std::unexpected(git::make_git_error(code));
            git::ReferencePtr owner(created);
        }
    } else {
        const char* sym_target = git_reference_symbolic_target(head_ref.get());
        if (sym_target != nullptr) {
            checkpoint.head_symbolic_ref = sym_target;
            git_reference* created = nullptr;
            const std::string head_backup_ref = backup_prefix + "/HEAD";
            const int code = git_reference_symbolic_create(&created, repo_.handle(), head_backup_ref.c_str(),
                                                           sym_target, 1, "gitdatax backup HEAD");
            if (code < 0) return std::unexpected(git::make_git_error(code));
            git::ReferencePtr owner(created);
        }
    }

    // 2. Zapisanie kopii każdej modyfikowanej gałęzi
    for (const auto& branch_ref_name : branch_ref_names) {
        git_reference* raw_branch = nullptr;
        const int branch_code = git_reference_lookup(&raw_branch, repo_.handle(), branch_ref_name.c_str());
        if (branch_code == GIT_ENOTFOUND) {
            continue; // Gałąź jeszcze nie istnieje
        }
        if (branch_code < 0) {
            return std::unexpected(git::make_git_error(branch_code));
        }
        git::ReferencePtr branch_ref(raw_branch);

        git_object* raw_commit = nullptr;
        const int peel_code = git_reference_peel(&raw_commit, branch_ref.get(), GIT_OBJECT_COMMIT);
        if (peel_code < 0) {
            return std::unexpected(git::make_git_error(peel_code));
        }
        git::ObjectPtr commit_obj(raw_commit);
        const git::Oid branch_target(*git_object_id(commit_obj.get()));

        checkpoint.branches.push_back(BackupEntry{
            .ref_name = branch_ref_name,
            .target_oid = branch_target
        });

        // Tworzenie fizycznej referencji zapasowej: refs/gitdatax/backup/<id>/<branch_ref_name>
        const std::string backup_ref_name = backup_prefix + "/" + branch_ref_name;
        git_reference* created = nullptr;
        const int create_code = git_reference_create(&created, repo_.handle(), backup_ref_name.c_str(),
                                                     &branch_target.raw(), 1, "gitdatax backup branch");
        if (create_code < 0) {
            return std::unexpected(git::make_git_error(create_code));
        }
        git::ReferencePtr owner(created);
    }

    return checkpoint;
}

git::GitResult<std::vector<BackupCheckpoint>> BackupManager::list_checkpoints() {
    git_reference_iterator* raw_it = nullptr;
    const int iter_code = git_reference_iterator_glob_new(&raw_it, repo_.handle(), "refs/gitdatax/backup/*");
    if (iter_code < 0) {
        return std::unexpected(git::make_git_error(iter_code));
    }
    git::ReferenceIteratorPtr it(raw_it);

    // Grupowanie referencji po checkpoint_id (używamy std::map dla posortowania chronologicznego)
    std::map<std::string, BackupCheckpoint> checkpoints_map;

    while (true) {
        git_reference* raw_ref = nullptr;
        const int next_code = git_reference_next(&raw_ref, it.get());
        if (next_code == GIT_ITEROVER) break;
        if (next_code < 0) return std::unexpected(git::make_git_error(next_code));
        git::ReferencePtr ref(raw_ref);

        const std::string full_name = git_reference_name(ref.get());
        const std::string prefix = "refs/gitdatax/backup/";
        if (!full_name.starts_with(prefix)) continue;

        const std::string remainder = full_name.substr(prefix.size());
        const auto slash_pos = remainder.find('/');
        if (slash_pos == std::string::npos) continue;

        const std::string chk_id = remainder.substr(0, slash_pos);
        const std::string sub_path = remainder.substr(slash_pos + 1);

        auto& chk = checkpoints_map[chk_id];
        chk.id = chk_id;

        if (sub_path == "HEAD") {
            if (git_reference_type(ref.get()) == GIT_REFERENCE_SYMBOLIC) {
                chk.is_head_detached = false;
                chk.head_symbolic_ref = git_reference_symbolic_target(ref.get());
            } else {
                chk.is_head_detached = true;
                const git_oid* target = git_reference_target(ref.get());
                if (target) chk.head_target = git::Oid(*target);
            }
        } else {
            // sub_path to np. "refs/heads/main"
            const git_oid* target = git_reference_target(ref.get());
            if (target) {
                chk.branches.push_back(BackupEntry{
                    .ref_name = sub_path,
                    .target_oid = git::Oid(*target)
                });
            }
        }
    }

    // Przepisujemy od najnowszego do najstarszego
    std::vector<BackupCheckpoint> result;
    result.reserve(checkpoints_map.size());
    for (auto it_map = checkpoints_map.rbegin(); it_map != checkpoints_map.rend(); ++it_map) {
        result.push_back(std::move(it_map->second));
    }
    return result;
}

git::GitResult<void> BackupManager::restore_checkpoint(const BackupCheckpoint& checkpoint) {
    // 1. Przywracamy gałęzie
    for (const auto& entry : checkpoint.branches) {
        git_reference* raw = nullptr;
        const int code = git_reference_create(&raw, repo_.handle(), entry.ref_name.c_str(),
                                              &entry.target_oid.raw(), 1, "gitdatax undo");
        if (code < 0) {
            return std::unexpected(git::make_git_error(code));
        }
        git::ReferencePtr owner(raw);
    }

    // 2. Przywracamy HEAD
    if (checkpoint.is_head_detached) {
        const int code = git_repository_set_head_detached(repo_.handle(), &checkpoint.head_target.raw());
        if (code < 0) return std::unexpected(git::make_git_error(code));
    } else if (!checkpoint.head_symbolic_ref.empty()) {
        const int code = git_repository_set_head(repo_.handle(), checkpoint.head_symbolic_ref.c_str());
        if (code < 0) return std::unexpected(git::make_git_error(code));
    }

    return {};
}

git::GitResult<void> BackupManager::restore_latest() {
    auto checkpoints = list_checkpoints();
    if (!checkpoints) {
        return std::unexpected(checkpoints.error());
    }
    if (checkpoints->empty()) {
        return std::unexpected(git::GitError{GIT_ENOTFOUND, "Brak dostepnych punktow kontrolnych do przywrocenia"});
    }

    return restore_checkpoint(checkpoints->front());
}

}  // namespace gitdatax::core
