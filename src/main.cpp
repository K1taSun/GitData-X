#include <iostream>
#include <optional>
#include <string_view>

#include "core/version.hpp"
#include "git/error.hpp"
#include "tui/app_controller.hpp"

int main(int argc, char* argv[]) {
    // 1. Inicjalizacja biblioteki libgit2 w mechanizmie RAII (globalny licznik referencji)
    gitdatax::git::GitLibInit git_init;

    // 2. Obsługa argumentów wiersza poleceń CLI
    std::optional<std::filesystem::path> repo_path;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--version" || arg == "-v") {
            std::cout << "GitData X wersja " << gitdatax::version() << '\n';
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "Uzycie: gitdatax [SCIEZKA_REPO]\n\n"
                      << "Opcje:\n"
                      << "  -v, --version  Wyswietl wersje programu\n"
                      << "  -h, --help     Wyswietl te pomoc\n\n"
                      << "Skroty klawiszowe TUI:\n"
                      << "  [Space]        Zaznacz commit\n"
                      << "  [1-6]          Wybierz jeden z 6 trybow manipulacji czasem\n"
                      << "  [s]            Rozdziel commit na K czesci (Split 1 -> K)\n"
                      << "  [R]            Przelosuj ziarno (seed) rozkladu czasu na zywo\n"
                      << "  [p]            Zatwierdz plan i zapisz w ODB (Zero Working-Tree Touch)\n"
                      << "  [u]            Cofnij zmiany (Atomowe Undo)\n"
                      << "  [q], [Esc]     Wyjdz z programu\n";
            return 0;
        }
        if (!arg.starts_with("-")) {
            repo_path = std::filesystem::path(arg);
        }
    }

    // 3. Uruchomienie interaktywnego interfejsu TUI 2026 TrueColor
    gitdatax::tui::AppController controller;
    return controller.run(repo_path);
}
