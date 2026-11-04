#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/commit_node.hpp"
#include "core/dag_snapshot.hpp"
#include "git/oid.hpp"

namespace gitdatax::testing {

// Tworzy deterministyczny Oid z liczby (np. 7 -> "000...007"), żeby testy były czytelne.
inline git::Oid oid_from_number(unsigned int number) {
    char buffer[git::Oid::kHexLength + 1];
    std::snprintf(buffer, sizeof(buffer), "%040x", number);
    return git::Oid::from_hex(buffer).value();
}

// Buduje commit testowy; id i czas to zwykłe liczby, a rodzice to numery innych commitów.
inline core::CommitNode make_node(unsigned int id, const std::vector<unsigned int>& parents,
                                  std::int64_t committer_time) {
    core::CommitNode node;
    node.id = oid_from_number(id);
    for (const unsigned int parent : parents) {
        node.parents.push_back(oid_from_number(parent));
    }
    node.committer.timestamp = committer_time;
    node.author.timestamp = committer_time;
    node.summary = "commit " + std::to_string(id);
    return node;
}

// Sprawdza niezmiennik z dokumentacji: każdy rodzic stoi przed dzieckiem.
inline bool every_parent_before_child(const core::DagSnapshot& snapshot,
                                      const std::vector<git::Oid>& sorted) {
    std::unordered_map<git::Oid, std::size_t> position;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        position[sorted[i]] = i;
    }
    for (const core::CommitNode& node : snapshot.commits()) {
        for (const git::Oid& parent : node.parents) {
            if (snapshot.contains(parent) && position.at(parent) >= position.at(node.id)) {
                return false;
            }
        }
    }
    return sorted.size() == snapshot.size();
}
}  // namespace gitdatax::testing
