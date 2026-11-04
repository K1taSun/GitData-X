#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>

namespace gitdatax::tui {

// Paleta barw "Modern TUI 2026" (Sekcja 6.1) w pełnym 24-bit TrueColor.
struct Theme2026 {
    // Tło bazowe Cyber-Slate
    static ftxui::Color BackgroundDark() { return ftxui::Color::RGB(17, 17, 27); }   // #11111B
    static ftxui::Color BackgroundLight() { return ftxui::Color::RGB(24, 24, 37); }  // #181825
    static ftxui::Color Surface() { return ftxui::Color::RGB(30, 30, 46); }          // #1E1E2E

    // Akcenty semantyczne
    static ftxui::Color Emerald() { return ftxui::Color::RGB(166, 227, 161); }      // #A6E3A1 - nowo wyliczone daty [MODIFIED]
    static ftxui::Color NeonViolet() { return ftxui::Color::RGB(203, 166, 247); }   // #CBA6F7 - [SPLIT 1->K] i pod-wierzcholki
    static ftxui::Color Amber() { return ftxui::Color::RGB(249, 226, 175); }        // #F9E2AF - kaskada hasha SHA [CASCADE]
    static ftxui::Color Coral() { return ftxui::Color::RGB(243, 139, 168); }        // #F38BA8 - ostrzezenia i anomalie
    static ftxui::Color Blue() { return ftxui::Color::RGB(137, 180, 250); }         // #89B4FA - referencje HEAD / galaz

    // Tekst
    static ftxui::Color TextPrimary() { return ftxui::Color::RGB(205, 214, 244); }  // #CDD6F4
    static ftxui::Color TextMuted() { return ftxui::Color::RGB(108, 112, 134); }    // #6C7086
};

// Semantyczne plakietki (badge)
ftxui::Element badge_modified();
ftxui::Element badge_cascade();
ftxui::Element badge_split(std::size_t k);
ftxui::Element badge_unchanged();
ftxui::Element badge_warning(const std::string& text);

}  // namespace gitdatax::tui
