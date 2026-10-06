#include "git/graph_loader.hpp"

#include <git2.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "git/oid.hpp"
#include "git/raii.hpp"

namespace gitdatax::git {

namespace {

using core::CommitNode;
using core::CommitSignature;
using core::HeadInfo;
using core::HeadKind;
using core::RefInfo;

std::string safe_string(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

CommitSignature copy_signature(const git_signature* signature) {
    CommitSignature copy;
    if (signature == nullptr) {
        return copy;
    }
    copy.name = safe_string(signature->name);
    copy.email = safe_string(signature->email);
    copy.timestamp = static_cast<std::int64_t>(signature->when.time);
    copy.tz_offset_min = signature->when.offset;
    return copy;
}

// Brak nagłówka to normalna sytuacja (GIT_ENOTFOUND), a nie błąd.
GitResult<std::optional<std::string>> read_header(git_commit* commit, const char* field) {
    GitBuf buffer;
    const int code = git_commit_header_field(buffer.get(), commit, field);
    if (code == GIT_ENOTFOUND) {
        return std::optional<std::string>{};
    }
    if (code < 0) {
        return std::unexpected(make_git_error(code));
    }
    return std::optional<std::string>(buffer.to_string());
}

GitResult<CommitNode> convert_commit(git_commit* commit) {
    CommitNode node;
    node.id = Oid(*git_commit_id(commit));
    node.tree_id = Oid(*git_commit_tree_id(commit));

    // Pętla po indeksach zachowuje dokładną kolejność rodziców.
    const unsigned int parent_count = git_commit_parentcount(commit);
    node.parents.reserve(parent_count);
    for (unsigned int i = 0; i < parent_count; ++i) {
        node.parents.emplace_back(*git_commit_parent_id(commit, i));
    }

    node.author = copy_signature(git_commit_author(commit));
    node.committer = copy_signature(git_commit_committer(commit));
    node.summary = safe_string(git_commit_summary(commit));
    node.body = safe_string(git_commit_body(commit));
    if (const char* encoding = git_commit_message_encoding(commit)) {
        node.message_encoding = std::string(encoding);
    }

    auto gpgsig = read_header(commit, "gpgsig");
    if (!gpgsig) {
        return std::unexpected(gpgsig.error());
    }
    node.has_gpgsig = gpgsig->has_value();

    auto mergetag = read_header(commit, "mergetag");
    if (!mergetag) {
        return std::unexpected(mergetag.error());
    }
    node.mergetag = std::move(*mergetag);

    return node;
}

// skip_non_commit: tagi mogą wskazywać drzewo lub blob, a gałęzie zawsze wskazują commit.
GitResult<std::vector<RefInfo>> collect_refs(git_repository* repo, const char* glob,
                                             bool skip_non_commit) {
    git_reference_iterator* raw_iterator = nullptr;
    if (const int code = git_reference_iterator_glob_new(&raw_iterator, repo, glob); code < 0) {
        return std::unexpected(make_git_error(code));
    }
    ReferenceIteratorPtr iterator(raw_iterator);

    std::vector<RefInfo> refs;
    while (true) {
        git_reference* raw_ref = nullptr;
        const int next_code = git_reference_next(&raw_ref, iterator.get());
        if (next_code == GIT_ITEROVER) {
            break;
        }
        if (next_code < 0) {
            return std::unexpected(make_git_error(next_code));
        }
        ReferencePtr ref(raw_ref);

        // Peel obiera tag adnotowany do commitu, na który ostatecznie wskazuje.
        git_object* raw_object = nullptr;
        const int peel_code = git_reference_peel(&raw_object, ref.get(), GIT_OBJECT_COMMIT);
        if (peel_code < 0) {
            if (skip_non_commit) {
                git_error_clear();
                continue;
            }
            return std::unexpected(make_git_error(peel_code));
        }
        ObjectPtr object(raw_object);
        refs.push_back(RefInfo{git_reference_name(ref.get()), Oid(*git_object_id(object.get()))});
    }

    // Stała kolejność niezależna od kolejności zwracanej przez libgit2.
    std::sort(refs.begin(), refs.end(),
              [](const RefInfo& lhs, const RefInfo& rhs) { return lhs.name < rhs.name; });
    return refs;
}

GitResult<HeadInfo> read_head(git_repository* repo) {
    HeadInfo head;

    const int detached = git_repository_head_detached(repo);
    if (detached < 0) {
        return std::unexpected(make_git_error(detached));
    }
    const int unborn = git_repository_head_unborn(repo);
    if (unborn < 0) {
        return std::unexpected(make_git_error(unborn));
    }

    git_reference* raw_ref = nullptr;
    if (const int code = git_reference_lookup(&raw_ref, repo, "HEAD"); code < 0) {
        return std::unexpected(make_git_error(code));
    }
    ReferencePtr ref(raw_ref);

    if (detached == 1) {
        head.kind = HeadKind::Detached;
        head.target = Oid(*git_reference_target(ref.get()));
        return head;
    }

    head.branch_ref = safe_string(git_reference_symbolic_target(ref.get()));
    if (unborn == 1) {
        head.kind = HeadKind::Unborn;
        return head;
    }

    git_oid resolved{};
    if (const int code = git_reference_name_to_id(&resolved, repo, "HEAD"); code < 0) {
        return std::unexpected(make_git_error(code));
    }
    head.kind = HeadKind::Symbolic;
    head.target = Oid(resolved);
    return head;
}

struct PendingCommit {
    Oid id;
    bool required;  // commit startowy musi istnieć; brakujący rodzic oznacza płytkie repozytorium
};

GitResult<std::vector<CommitNode>> read_reachable_commits(git_repository* repo,
                                                          const std::vector<Oid>& starts) {
    std::vector<CommitNode> nodes;
    std::unordered_set<Oid> visited;

    // Stos zamiast rekurencji: długa historia nie przepełni stosu wywołań.
    std::vector<PendingCommit> stack;
    for (auto it = starts.rbegin(); it != starts.rend(); ++it) {
        stack.push_back(PendingCommit{*it, true});
    }

    while (!stack.empty()) {
        const PendingCommit pending = stack.back();
        stack.pop_back();
        if (!visited.insert(pending.id).second) {
            continue;
        }

        git_commit* raw_commit = nullptr;
        const int code = git_commit_lookup(&raw_commit, repo, &pending.id.raw());
        if (code == GIT_ENOTFOUND && !pending.required) {
            git_error_clear();
            continue;
        }
        if (code < 0) {
            return std::unexpected(make_git_error(code));
        }
        CommitPtr commit(raw_commit);

        auto node = convert_commit(commit.get());
        if (!node) {
            return std::unexpected(node.error());
        }

        // Odwrócona kolejność: pierwszy rodzic trafia na wierzch stosu i jest odwiedzony pierwszy.
        for (auto it = node->parents.rbegin(); it != node->parents.rend(); ++it) {
            stack.push_back(PendingCommit{*it, false});
        }
        nodes.push_back(std::move(*node));
    }
    return nodes;
}

}  // namespace

GitResult<core::DagSnapshot> load_dag_snapshot(const Repo& repo) {
    git_repository* raw_repo = repo.handle();

    auto head = read_head(raw_repo);
    if (!head) {
        return std::unexpected(head.error());
    }
    auto branches = collect_refs(raw_repo, "refs/heads/*", false);
    if (!branches) {
        return std::unexpected(branches.error());
    }
    auto tags = collect_refs(raw_repo, "refs/tags/*", true);
    if (!tags) {
        return std::unexpected(tags.error());
    }

    std::vector<Oid> starts;
    if (head->target.has_value()) {
        starts.push_back(*head->target);
    }
    for (const RefInfo& branch : *branches) {
        starts.push_back(branch.target);
    }
    for (const RefInfo& tag : *tags) {
        starts.push_back(tag.target);
    }

    auto commits = read_reachable_commits(raw_repo, starts);
    if (!commits) {
        return std::unexpected(commits.error());
    }

    return core::DagSnapshot(std::move(*commits), std::move(*branches), std::move(*tags),
                             std::move(*head));
}

}  // namespace gitdatax::git

