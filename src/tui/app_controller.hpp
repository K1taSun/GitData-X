#pragma once

#include <filesystem>
#include <optional>

namespace gitdatax::tui {

// Główny kontroler aplikacji TUI i pętli zdarzeń (Sekcja 6 i 8.1).
class AppController {
public:
    AppController() = default;

    // Uruchamia pełny interfejs TrueColor FTXUI.
    // Zero-Config Start: jeśli repo_path nie jest podane, przeszukuje CWD i katalogi nadrzędne.
    int run(std::optional<std::filesystem::path> repo_path = std::nullopt);
};

}  // namespace gitdatax::tui
