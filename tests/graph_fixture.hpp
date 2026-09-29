#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/commit_node.hpp"
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

}  // namespace gitdatax::testing
