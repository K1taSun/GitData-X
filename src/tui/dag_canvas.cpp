#include "tui/dag_canvas.hpp"

#include "core/time_utils.hpp"
#include "tui/theme_2026.hpp"

namespace gitdatax::tui {

using namespace ftxui;

Element DagCanvas::render(
    const core::DagSnapshot& dag,
    const std::vector<git::Oid>& sorted_commit_ids,
    const std::unordered_map<git::Oid, core::CommitTransformType>& cascade_status,
    const core::RewritePlanner& planner,
    std::size_t selected_index) {

    Elements rows;

    for (std::size_t i = 0; i < sorted_commit_ids.size(); ++i) {
        const git::Oid& id = sorted_commit_ids[i];
        const core::CommitNode* node = dag.find(id);
        if (node == nullptr) continue;

        const bool is_selected = (i == selected_index);

        // Wyznaczenie statusu kaskadowego
        core::CommitTransformType type = core::CommitTransformType::Unchanged;
        const auto it_stat = cascade_status.find(id);
        if (it_stat != cascade_status.end()) {
            type = it_stat->second;
        }

        Element badge_elem = badge_unchanged();
        if (type == core::CommitTransformType::Modify1to1) {
            badge_elem = badge_modified();
        } else if (type == core::CommitTransformType::Split1toK) {
            const auto* plan = planner.find_action(id);
            std::size_t k = (plan && plan->split_plan) ? plan->split_plan->K : 2;
            badge_elem = badge_split(k);
        } else if (type == core::CommitTransformType::CascadeShaOnly) {
            badge_elem = badge_cascade();
        }

        // Referencje wskazujące na dany commit
        Elements ref_elements;
        if (dag.head().target.has_value() && *dag.head().target == id) {
            std::string head_desc = "HEAD";
            if (dag.head().kind == core::HeadKind::Symbolic) {
                head_desc += " -> " + dag.head().branch_ref;
            }
            ref_elements.push_back(text("[" + head_desc + "] ") | color(Theme2026::Blue()) | bold);
        }
        for (const auto& br : dag.branches()) {
            if (br.target == id) {
                ref_elements.push_back(text("(" + br.name + ") ") | color(Theme2026::Emerald()));
            }
        }

        const std::string short_sha = id.to_hex().substr(0, 7);
        const auto* plan = planner.find_action(id);

        // Data (oryginalna lub nowo wyliczona)
        core::TimePoint effective_time{
            .timestamp = node->committer.timestamp,
            .tz_offset_min = node->committer.tz_offset_min
        };
        if (plan && plan->new_committer.has_value()) {
            effective_time.timestamp = plan->new_committer->timestamp;
            effective_time.tz_offset_min = plan->new_committer->tz_offset_min;
        }
        const std::string date_str = core::format_iso8601(effective_time);

        // Węzeł commitu (symbol kropki)
        const std::string dot = (type == core::CommitTransformType::Split1toK) ? "◎ " : "● ";
        Color dot_color = Theme2026::TextPrimary();
        if (type == core::CommitTransformType::Modify1to1) dot_color = Theme2026::Emerald();
        if (type == core::CommitTransformType::Split1toK) dot_color = Theme2026::NeonViolet();
        if (type == core::CommitTransformType::CascadeShaOnly) dot_color = Theme2026::Amber();

        auto row = hbox({
            text(is_selected ? " ▸ " : "   ") | color(Theme2026::Emerald()) | bold,
            text(dot) | color(dot_color) | bold,
            text(short_sha + " ") | color(Theme2026::TextPrimary()) | bold,
            hbox(std::move(ref_elements)),
            badge_elem,
            text(" " + date_str + " ") | color(Theme2026::TextMuted()),
            text(node->summary) | color(Theme2026::TextPrimary())
        });

        if (is_selected) {
            row = row | bgcolor(Theme2026::BackgroundLight());
        }
        rows.push_back(row);

        // Jeśli commit jest zaplanowany do rozbicia (Split 1 -> K), renderujemy wirtualne pod-wierzchołki
        if (type == core::CommitTransformType::Split1toK && plan && plan->split_plan.has_value()) {
            const auto& sp = *plan->split_plan;
            for (std::size_t s = 0; s < sp.steps.size(); ++s) {
                const bool is_last_step = (s == sp.steps.size() - 1);
                const std::string branch_symbol = is_last_step ? "      └─◈ " : "      ├─◈ ";
                std::string file_info;
                if (!sp.steps[s].completed_files.empty()) {
                    file_info = sp.steps[s].completed_files.front().path;
                }

                core::TimePoint step_tp{
                    .timestamp = sp.steps[s].committer.timestamp,
                    .tz_offset_min = sp.steps[s].committer.tz_offset_min
                };

                rows.push_back(hbox({
                    text(branch_symbol) | color(Theme2026::NeonViolet()),
                    text("[" + std::to_string(s + 1) + "/" + std::to_string(sp.K) + "] ") |
                        color(Theme2026::NeonViolet()) | bold,
                    text(core::format_iso8601(step_tp) + " ") | color(Theme2026::TextMuted()),
                    text(file_info.empty() ? sp.steps[s].message : file_info) | color(Theme2026::TextPrimary())
                }));
            }
        }
    }

    if (rows.empty()) {
        rows.push_back(text(" (Brak commitow w repozytorium) ") | color(Theme2026::TextMuted()));
    }

    return vbox(std::move(rows));
}

}  // namespace gitdatax::tui
