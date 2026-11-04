#include "core/rewrite_engine.hpp"

#include <git2.h>

#include "core/backup_manager.hpp"
#include "core/dag_verifier.hpp"
#include "git/commit.hpp"
#include "git/in_memory_splitter.hpp"
#include "git/transaction.hpp"

namespace gitdatax::core {

RewriteEngine::RewriteEngine(git::Repo& repo, const DagSnapshot& dag)
    : repo_(repo), dag_(dag) {}

git::GitResult<RewriteExecutionReport> RewriteEngine::execute(const RewritePlanner& planner) {
    // 1. Kontrola stanu repozytorium (zablokowanie przy trwającym rebase/merge)
    if (repo_.is_read_only()) {
        return std::unexpected(git::GitError{
            GIT_ERROR,
            "Operacja zapisu zablokowana: repozytorium znajduje sie w trybie tylko do odczytu (stan operacji Gita w toku)"
        });
    }

    const auto affected = planner.affected_branches();
    if (affected.empty()) {
        return RewriteExecutionReport{};
    }

    // 2. Automatyczny punkt kontrolny BackupManager przed zatwierdzeniem
    BackupManager backup_mgr(repo_);
    auto checkpoint = backup_mgr.create_checkpoint(affected);
    if (!checkpoint) {
        return std::unexpected(checkpoint.error());
    }

    // 3. Wczytanie i posortowanie podgrafu w porządku topologicznym Kahna
    const auto sorted = dag_.topological_sort_oldest_first();
    if (!sorted) {
        return std::unexpected(git::GitError{GIT_ERROR, sorted.error()});
    }

    git::InMemorySplitter splitter(repo_);
    const auto empty_tree = splitter.empty_tree_oid();
    if (!empty_tree) {
        return std::unexpected(empty_tree.error());
    }

    std::unordered_map<git::Oid, std::vector<git::Oid>> chain_map;
    std::unordered_map<git::Oid, CommitNode> new_nodes;

    // 4. Algorytm przepisywania Kahna (Sekcja 3.2)
    for (const git::Oid& commit_id : *sorted) {
        const CommitNode* orig = dag_.find(commit_id);
        if (orig == nullptr) continue;

        // Mapowanie wejściowych rodziców: każdy rodzic r wskazuje na koniec swojego łańcucha chain[r].back()
        std::vector<git::Oid> new_input_parents;
        new_input_parents.reserve(orig->parents.size());
        for (const git::Oid& p : orig->parents) {
            const auto it = chain_map.find(p);
            new_input_parents.push_back((it != chain_map.end()) ? it->second.back() : p);
        }

        const CommitRewritePlan* plan = planner.find_action(commit_id);
        const bool changed = (plan != nullptr) || (new_input_parents != orig->parents);

        if (!changed) {
            // Hash SHA pozostaje nienaruszony
            chain_map[commit_id] = {commit_id};
            new_nodes[commit_id] = *orig;
            continue;
        }

        if (plan != nullptr && plan->type == CommitTransformType::Split1toK && plan->split_plan.has_value()) {
            // --- SPLIT 1 -> K (Sekcja 5) ---
            const auto& sp = *plan->split_plan;
            std::vector<git::Oid> chain;
            chain.reserve(sp.K);

            std::vector<git::Oid> cur_parents = new_input_parents;
            git::Oid cur_tree = orig->parents.empty()
                                    ? *empty_tree
                                    : dag_.find(orig->parents[0])->tree_id;

            for (std::size_t k = 0; k < sp.K; ++k) {
                git::Oid step_tree;
                if (k < sp.K - 1) {
                    git::SplitStepAssignment assign{
                        .step_index = k,
                        .completed_files = sp.steps[k].completed_files,
                        .partial_hunks = sp.steps[k].partial_hunks
                    };
                    auto st = splitter.synthesize_intermediate_tree(cur_tree, assign, orig->tree_id);
                    if (!st) return std::unexpected(st.error());
                    step_tree = *st;
                    cur_tree = step_tree;
                } else {
                    // ZŁOTA REGUŁA KROKU K: Ostatnie ogniwo zawsze otrzymuje oryginalne drzewo commitu!
                    step_tree = orig->tree_id;
                }

                git::CreateCommitParams commit_params{
                    .tree_id = step_tree,
                    .parent_ids = cur_parents,
                    .author = sp.steps[k].author,
                    .committer = sp.steps[k].committer,
                    .message = sp.steps[k].message,
                    .message_encoding = orig->message_encoding
                };

                auto new_oid = git::create_commit_in_odb(repo_, commit_params);
                if (!new_oid) return std::unexpected(new_oid.error());

                chain.push_back(*new_oid);
                cur_parents = {*new_oid};

                CommitNode node;
                node.id = *new_oid;
                node.tree_id = step_tree;
                node.parents = commit_params.parent_ids;
                node.author = commit_params.author;
                node.committer = commit_params.committer;
                node.summary = sp.steps[k].message;
                new_nodes[*new_oid] = std::move(node);
            }

            chain_map[commit_id] = std::move(chain);
        } else {
            // --- Transformacja 1 -> 1 (modyfikacja lub kaskada SHA) ---
            CommitSignature author = (plan && plan->new_author) ? *plan->new_author : orig->author;
            CommitSignature committer = (plan && plan->new_committer) ? *plan->new_committer : orig->committer;
            std::string message = (plan && plan->new_message)
                                      ? *plan->new_message
                                      : (orig->summary + (orig->body.empty() ? "" : "\n\n" + orig->body) + "\n");

            git::CreateCommitParams commit_params{
                .tree_id = orig->tree_id,
                .parent_ids = new_input_parents,
                .author = author,
                .committer = committer,
                .message = message,
                .message_encoding = orig->message_encoding
            };

            auto new_oid = git::create_commit_in_odb(repo_, commit_params);
            if (!new_oid) return std::unexpected(new_oid.error());

            chain_map[commit_id] = {*new_oid};

            CommitNode node;
            node.id = *new_oid;
            node.tree_id = orig->tree_id;
            node.parents = new_input_parents;
            node.author = author;
            node.committer = committer;
            node.summary = message;
            new_nodes[*new_oid] = std::move(node);
        }
    }

    // 5. Formalna weryfikacja wszystkich 4 niezmienników (DagVerifier)
    const auto verif = DagVerifier::verify(dag_, chain_map, new_nodes);
    if (!verif) {
        return std::unexpected(git::GitError{
            GIT_ERROR,
            "Weryfikacja formalna nie powiodla sie: " + verif.error().invariant_name + " - " + verif.error().details
        });
    }

    // 6. Atomowa transakcja aktualizacji referencji (CAS)
    git::RefTransaction tx(repo_);
    for (const auto& branch : dag_.branches()) {
        const auto it = chain_map.find(branch.target);
        if (it != chain_map.end() && !it->second.empty()) {
            const git::Oid new_target = it->second.back();
            if (new_target != branch.target) {
                const auto add_res = tx.add_branch_update(git::BranchUpdate{
                    .ref_name = branch.name,
                    .expected_old_oid = branch.target,
                    .new_oid = new_target
                });
                if (!add_res) return std::unexpected(add_res.error());
            }
        }
    }

    // Aktualizacja HEAD
    if (dag_.head().kind == HeadKind::Detached && dag_.head().target.has_value()) {
        const auto it = chain_map.find(*dag_.head().target);
        if (it != chain_map.end() && !it->second.empty()) {
            tx.set_detached_head_target(it->second.back());
        }
    } else if (dag_.head().kind == HeadKind::Symbolic) {
        tx.set_symbolic_head_target(dag_.head().branch_ref);
    }

    const auto tx_commit = tx.commit();
    if (!tx_commit) {
        return std::unexpected(tx_commit.error());
    }

    RewriteExecutionReport report;
    report.original_commit_count = dag_.size();
    report.rewritten_commit_count = new_nodes.size();
    report.chain_map = std::move(chain_map);
    report.updated_branches = affected;
    report.backup_checkpoint_id = checkpoint->id;

    return report;
}

}  // namespace gitdatax::core
