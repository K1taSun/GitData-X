#include "tui/date_modals.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "tui/theme_2026.hpp"

namespace gitdatax::tui {

using namespace ftxui;

std::string generate_sparkline(const std::vector<std::int64_t>& timestamps, std::size_t width) {
    if (timestamps.empty() || width == 0) {
        return std::string(width, ' ');
    }

    const auto [min_it, max_it] = std::minmax_element(timestamps.begin(), timestamps.end());
    const std::int64_t min_ts = *min_it;
    const std::int64_t max_ts = *max_it;

    std::vector<std::size_t> bins(width, 0);

    if (max_ts == min_ts) {
        bins[width / 2] = timestamps.size();
    } else {
        const double span = static_cast<double>(max_ts - min_ts);
        for (const std::int64_t ts : timestamps) {
            std::size_t bin_idx = static_cast<std::size_t>(
                (static_cast<double>(ts - min_ts) / span) * static_cast<double>(width - 1));
            bin_idx = std::min(bin_idx, width - 1);
            ++bins[bin_idx];
        }
    }

    const std::size_t max_count = *std::max_element(bins.begin(), bins.end());
    if (max_count == 0) {
        return std::string(width, ' ');
    }

    static const char* const kBlocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    constexpr std::size_t kNumLevels = 8;

    std::string sparkline;
    for (const std::size_t count : bins) {
        const std::size_t level = (count * kNumLevels) / max_count;
        sparkline.append(kBlocks[std::min(level, kNumLevels)]);
    }

    return sparkline;
}

Element DateModals::render_strategy_summary(
    int mode,
    const std::string& description,
    const std::string& sparkline_data,
    std::uint64_t seed) {

    std::stringstream seed_ss;
    seed_ss << "#" << std::uppercase << std::hex << (seed & 0xFFFF);

    return vbox({
        hbox({
            text(" Tryb [" + std::to_string(mode) + "]: ") | color(Theme2026::Emerald()) | bold,
            text(description) | color(Theme2026::TextPrimary())
        }),
        separator(),
        hbox({
            text(" Rozklad: ") | color(Theme2026::TextMuted()),
            text(sparkline_data) | color(Theme2026::Emerald()) | bold,
            text("  [Seed: " + seed_ss.str() + "]") | color(Theme2026::TextMuted())
        })
    }) | borderRounded | bgcolor(Theme2026::Surface());
}

}  // namespace gitdatax::tui
