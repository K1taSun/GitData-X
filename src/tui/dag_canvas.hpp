#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/commit_node.hpp"
#include "core/dag_snapshot.hpp"
#include "core/rewrite_planner.hpp"
#include "git/oid.hpp"

namespace gitdatax::tui {

// Renderuje panel z historią commitów i podglądem na żywo kaskady SHA (Sekcja 6.2).
class DagCanvas {
public:
    static ftxui::Element render(
        const core::DagSnapshot& dag,
        const std::vector<git::Oid>& sorted_commit_ids,
        const std::unordered_map<git::Oid, core::CommitTransformType>& cascade_status,
        const core::RewritePlanner& planner,
        std::size_t selected_index
    );
};

}  // namespace gitdatax::tui
