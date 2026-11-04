#pragma once

#include <cstddef>
#include <ftxui/dom/elements.hpp>
#include <string>

#include "core/commit_splitter.hpp"
#include "git/oid.hpp"

namespace gitdatax::tui {

// Interaktywny kreator rozdzielania zmian w kodzie (Sekcja 5 i 6.2).
class SplitWizard {
public:
    static ftxui::Element render(
        const git::Oid& commit_id,
        std::size_t K,
        bool level_files,
        bool level_hunks,
        bool level_lines,
        const core::SplitPlan* current_plan
    );
};

}  // namespace gitdatax::tui
