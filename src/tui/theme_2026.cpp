#include "tui/theme_2026.hpp"

namespace gitdatax::tui {

using namespace ftxui;

Element badge_modified() {
    return text(" [MODIFIED] ") | color(Theme2026::BackgroundDark()) |
           bgcolor(Theme2026::Emerald()) | bold;
}

Element badge_cascade() {
    return text(" [CASCADE] ") | color(Theme2026::BackgroundDark()) |
           bgcolor(Theme2026::Amber()) | bold;
}

Element badge_split(std::size_t k) {
    return text(" [SPLIT 1->" + std::to_string(k) + "] ") |
           color(Theme2026::BackgroundDark()) | bgcolor(Theme2026::NeonViolet()) | bold;
}

Element badge_unchanged() {
    return text(" [UNCHANGED] ") | color(Theme2026::TextMuted());
}

Element badge_warning(const std::string& text_content) {
    return text(" [" + text_content + "] ") | color(Theme2026::BackgroundDark()) |
           bgcolor(Theme2026::Coral()) | bold;
}

}  // namespace gitdatax::tui
