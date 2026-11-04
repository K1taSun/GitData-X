#include "core/dag_snapshot.hpp"

#include <set>
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

    // Rodzica spoza zrzutu (np. pÅ‚ytkie repozytorium) pomijamy: nie ma do niego krawÄ™dzi.
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

std::expected<std::vector<git::Oid>, std::string> DagSnapshot::topological_sort_oldest_first() const {
    // in_degree[i] = ile rodzicÃ³w commitu i jest jeszcze nieprzetworzonych.
    std::vector<std::size_t> in_degree(commits_.size(), 0);
    for (std::size_t i = 0; i < commits_.size(); ++i) {
        for (const git::Oid& parent_id : commits_[i].parents) {
            if (index_by_id_.contains(parent_id)) {
                ++in_degree[i];
            }
        }
    }

    // std::set utrzymuje kolejkÄ™ gotowych commitÃ³w zawsze posortowanÄ…, wiÄ™c wynik
    // nie zaleÅ¼y od kolejnoÅ›ci wczytania ani od implementacji unordered_map.
    const auto older_first = [this](std::size_t lhs, std::size_t rhs) {
        const CommitNode& a = commits_[lhs];
        const CommitNode& b = commits_[rhs];
        if (a.committer.timestamp != b.committer.timestamp) {
            return a.committer.timestamp < b.committer.timestamp;
        }
        return a.id < b.id;
    };
    std::set<std::size_t, decltype(older_first)> ready(older_first);
    for (std::size_t i = 0; i < commits_.size(); ++i) {
        if (in_degree[i] == 0) {
            ready.insert(i);
        }
    }

    std::vector<git::Oid> sorted;
    sorted.reserve(commits_.size());
    while (!ready.empty()) {
        const std::size_t current = *ready.begin();
        ready.erase(ready.begin());
        sorted.push_back(commits_[current].id);

        // KrawÄ™dÅº moÅ¼e wystÄ…piÄ‡ wielokrotnie (duplikat rodzica), a in_degree liczyÅ‚ jÄ… tak samo.
        for (const std::size_t child : children_[current]) {
            --in_degree[child];
            if (in_degree[child] == 0) {
                ready.insert(child);
            }
        }
    }

    // Commity z in_degree > 0 po zakoÅ„czeniu pÄ™tli leÅ¼Ä… na cyklu.
    if (sorted.size() != commits_.size()) {
        return std::unexpected("wykryto cykl w grafie commitow");
    }
    return sorted;
}

}  // namespace gitdatax::core
