#include "tui/split_wizard.hpp"

#include "core/time_utils.hpp"
#include "tui/theme_2026.hpp"

namespace gitdatax::tui {

using namespace ftxui;

Element SplitWizard::render(
    const git::Oid& commit_id,
    std::size_t K,
    bool level_files,
    bool level_hunks,
    bool level_lines,
    const core::SplitPlan* current_plan) {

    Elements content;

    const std::string short_id = commit_id.is_zero() ? "brak" : commit_id.to_hex().substr(0, 7);

    content.push_back(hbox({
        text(" Commit: ") | color(Theme2026::TextMuted()),
        text(short_id) | color(Theme2026::TextPrimary()) | bold,
        text(" -> Na K = [ ") | color(Theme2026::TextMuted()),
        text(std::to_string(K)) | color(Theme2026::NeonViolet()) | bold,
        text(" ]") | color(Theme2026::TextMuted())
    }));

    content.push_back(hbox({
        text(" Poziom: ") | color(Theme2026::TextMuted()),
        text(level_files ? "[x] Pliki " : "[ ] Pliki ") | color(level_files ? Theme2026::Emerald() : Theme2026::TextMuted()),
        text(level_hunks ? "[x] Hunki " : "[ ] Hunki ") | color(level_hunks ? Theme2026::Emerald() : Theme2026::TextMuted()),
        text(level_lines ? "[x] Linie " : "[ ] Linie ") | color(level_lines ? Theme2026::Emerald() : Theme2026::TextMuted())
    }));

    content.push_back(separator());

    if (current_plan != nullptr && !current_plan->steps.empty()) {
        for (std::size_t i = 0; i < current_plan->steps.size(); ++i) {
            const auto& step = current_plan->steps[i];
            core::TimePoint tp{
                .timestamp = step.committer.timestamp,
                .tz_offset_min = step.committer.tz_offset_min
            };

            content.push_back(hbox({
                text(" ▸ [Czesc " + std::to_string(i + 1) + "/" + std::to_string(current_plan->K) + "] ") |
                    color(Theme2026::NeonViolet()) | bold,
                text(core::format_iso8601(tp)) | color(Theme2026::TextMuted())
            }));

            if (!step.completed_files.empty()) {
                for (const auto& file : step.completed_files) {
                    content.push_back(hbox({
                        text("   └─ ") | color(Theme2026::NeonViolet()),
                        text(file.path) | color(Theme2026::TextPrimary()),
                        text(" (caly plik)") | color(Theme2026::TextMuted())
                    }));
                }
            } else {
                content.push_back(text("   └─ (brak przypisanych plikow)") | color(Theme2026::TextMuted()));
            }
        }
    } else {
        content.push_back(text(" [s] Nacisnij klawisz, aby wygenerowac plan podzialu ") |
                          color(Theme2026::TextMuted()));
    }

    return vbox(std::move(content)) | borderRounded | bgcolor(Theme2026::BackgroundLight());
}

}  // namespace gitdatax::tui
