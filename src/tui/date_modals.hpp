#pragma once

#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <string>
#include <vector>

#include "core/time_strategies.hpp"

namespace gitdatax::tui {

// Generator wykresu słupkowego w tekście (Sparkline) na żywo przy użyciu bloków Unicode (Sekcja 6.2).
// Znakomicie wizualizuje gęstość commitów w czasie (np. ░░▂▄█▆▂░░).
std::string generate_sparkline(const std::vector<std::int64_t>& timestamps, std::size_t width = 16);

// Kreator 6 trybów manipulacji czasem (Sekcja 4 i 6.2).
class DateModals {
public:
    static ftxui::Element render_strategy_summary(int mode, const std::string& description,
                                                  const std::string& sparkline_data,
                                                  std::uint64_t seed);
};

}  // namespace gitdatax::tui
