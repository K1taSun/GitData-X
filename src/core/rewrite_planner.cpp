#include "core/rewrite_planner.hpp"

namespace gitdatax::core {

RewritePlanner::RewritePlanner(const DagSnapshot& dag) : dag_(dag) {}

void RewritePlanner::set_modify_action(
    const git::Oid& id,
    std::optional<CommitSignature> author,
    std::optional<CommitSignature> committer,
    std::optional<std::string> message) {
    CommitRewritePlan plan;
    plan.original_id = id;
    plan.type = CommitTransformType::Modify1to1;
    plan.new_author = std::move(author);
    plan.new_committer = std::move(committer);
    plan.new_message = std::move(message);
    explicit_plans_[id] = std::move(plan);
}

void RewritePlanner::set_split_action(const git::Oid& id, SplitPlan plan) {
    CommitRewritePlan rewrite_plan;
    rewrite_plan.original_id = id;
    rewrite_plan.type = CommitTransformType::Split1toK;
    rewrite_plan.split_plan = std::move(plan);
    explicit_plans_[id] = std::move(rewrite_plan);
}

void RewritePlanner::clear_action(const git::Oid& id) {
    explicit_plans_.erase(id);
}

const CommitRewritePlan* RewritePlanner::find_action(const git::Oid& id) const {
    const auto it = explicit_plans_.find(id);
    return it == explicit_plans_.end() ? nullptr : &it->second;
}

std::unordered_map<git::Oid, CommitTransformType> RewritePlanner::simulate_cascade() const {
    std::unordered_map<git::Oid, CommitTransformType> result;
    const auto sorted = dag_.topological_sort_oldest_first();
    if (!sorted.has_value()) {
        return result;
    }

    for (const git::Oid& commit_id : *sorted) {
        const CommitNode* node = dag_.find(commit_id);
        if (node == nullptr) continue;

        bool parents_changed = false;
        for (const git::Oid& p_id : node->parents) {
            const auto it_p = result.find(p_id);
            if (it_p != result.end() && it_p->second != CommitTransformType::Unchanged) {
                parents_changed = true;
                break;
            }
        }

        const auto it_plan = explicit_plans_.find(commit_id);
        if (it_plan != explicit_plans_.end()) {
            result[commit_id] = it_plan->second.type;
        } else if (parents_changed) {
            result[commit_id] = CommitTransformType::CascadeShaOnly;
        } else {
            result[commit_id] = CommitTransformType::Unchanged;
        }
    }

    return result;
}

std::vector<std::string> RewritePlanner::affected_branches() const {
    const auto cascade = simulate_cascade();
    std::vector<std::string> affected;

    for (const auto& branch : dag_.branches()) {
        const auto it = cascade.find(branch.target);
        if (it != cascade.end() && it->second != CommitTransformType::Unchanged) {
            affected.push_back(branch.name);
        }
    }

    return affected;
}

}  // namespace gitdatax::core
