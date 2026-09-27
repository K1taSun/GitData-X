#include "core/dag_snapshot.hpp"

#include <stdexcept>
#include <utility>

namespace gitdatax::core {

DagSnapshot::DagSnapshot(std::vector<CommitNode> commits, std::vector<RefInfo> branches,
                         std::vector<RefInfo> tags, HeadInfo head)
    : commits_(std::move(commits)),
      branches_(std::move(branches)),
      tags_(std::move(tags)),
      head_(std::move(head)),
      children_(commits_.size()) {
    index_by_id_.reserve(commits_.size());
    for (std::size_t i = 0; i < commits_.size(); ++i) {
        const bool inserted = index_by_id_.emplace(commits_[i].id, i).second;
        if (!inserted) {
            throw std::invalid_argument("DagSnapshot: zduplikowany commit " + commits_[i].id.to_hex());
        }
    }

    // Rodzica spoza zrzutu (np. płytkie repozytorium) pomijamy: nie ma do niego krawędzi.
    for (std::size_t child = 0; child < commits_.size(); ++child) {
        for (const git::Oid& parent_id : commits_[child].parents) {
            const auto parent = index_by_id_.find(parent_id);
            if (parent != index_by_id_.end()) {
                children_[parent->second].push_back(child);
            }
        }
    }
}

bool DagSnapshot::contains(const git::Oid& id) const {
    return index_by_id_.contains(id);
}

const CommitNode* DagSnapshot::find(const git::Oid& id) const {
    const auto it = index_by_id_.find(id);
    return it == index_by_id_.end() ? nullptr : &commits_[it->second];
}

std::vector<git::Oid> DagSnapshot::children_of(const git::Oid& id) const {
    std::vector<git::Oid> result;
    const auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) {
        return result;
    }
    result.reserve(children_[it->second].size());
    for (const std::size_t child : children_[it->second]) {
        result.push_back(commits_[child].id);
    }
    return result;
}

}  // namespace gitdatax::core
