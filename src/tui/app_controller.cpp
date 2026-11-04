#include "tui/app_controller.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <memory>

#include "core/backup_manager.hpp"
#include "core/commit_splitter.hpp"
#include "core/dag_snapshot.hpp"
#include "core/rewrite_engine.hpp"
#include "core/rewrite_planner.hpp"
#include "core/time_strategies.hpp"
#include "git/graph_loader.hpp"
#include "git/in_memory_splitter.hpp"
#include "git/repo.hpp"
#include "tui/dag_canvas.hpp"
#include "tui/date_modals.hpp"
#include "tui/split_wizard.hpp"
#include "tui/theme_2026.hpp"

namespace gitdatax::tui {

using namespace ftxui;

int AppController::run(std::optional<std::filesystem::path> repo_path) {
    // 1. Zero-Config Start: wykrycie repozytorium
    git::GitResult<git::Repo> repo_res = repo_path.has_value()
                                             ? git::Repo::open_from_path(*repo_path)
                                             : git::Repo::open_from_cwd();
    if (!repo_res.has_value()) {
        std::cerr << "Blad: Nie znaleziono repozytorium Gita w biezacym katalogu.\n"
                  << "Szczegoly: " << repo_res.error().message << "\n";
        return 1;
    }
    auto& repo = *repo_res;

    // 2. Wczytanie grafu commitów do pamięci RAM
    auto dag_res = git::load_dag_snapshot(repo);
    if (!dag_res.has_value()) {
        std::cerr << "Blad wczytywania grafu commitow: " << dag_res.error().message << "\n";
        return 1;
    }
    auto dag = std::make_unique<core::DagSnapshot>(std::move(*dag_res));

    auto sorted_res = dag->topological_sort_oldest_first();
    if (!sorted_res.has_value()) {
        std::cerr << "Blad sortowania topologicznego Kahna: " << sorted_res.error() << "\n";
        return 1;
    }
    std::vector<git::Oid> sorted_commits = *sorted_res;

    auto planner = std::make_unique<core::RewritePlanner>(*dag);

    std::size_t selected_index = 0;
    std::uint64_t current_seed = 0x91F4;
    int active_date_mode = 6;
    std::string status_notification = "Gotowy";

    std::size_t split_K = 3;
    core::SplitPlan current_split_plan;

    auto screen = ScreenInteractive::Fullscreen();

    // Główny renderer interfejsu (Makieta Sekcja 6.2)
    auto renderer = Renderer([&] {
        const auto cascade_map = planner->simulate_cascade();

        // 1. Panel lewy: Graf DAG
        auto dag_panel = DagCanvas::render(
            *dag, sorted_commits, cascade_map, *planner, selected_index
        ) | flex;

        // 2. Panel prawy: Kreator Rozdzielania
        git::Oid current_selected_id = sorted_commits.empty() ? git::Oid{} : sorted_commits[selected_index];
        const auto* act = planner->find_action(current_selected_id);
        const core::SplitPlan* p_plan = (act && act->split_plan) ? &*act->split_plan : nullptr;

        auto split_panel = SplitWizard::render(
            current_selected_id, split_K, true, true, false, p_plan
        ) | size(WIDTH, GREATER_THAN, 40);

        // 3. Pasek rozkładu losowego (Sparkline)
        std::vector<std::int64_t> sample_times;
        for (const auto& c_id : sorted_commits) {
            if (const auto* n = dag->find(c_id)) {
                sample_times.push_back(n->committer.timestamp);
            }
        }
        const std::string sparkline_str = generate_sparkline(sample_times, 16);

        std::string mode_desc = "Odcinkowy Random Shift do Pelnej Daty";
        if (active_date_mode == 1) mode_desc = "Data bezwzgledna / wzorzec";
        if (active_date_mode == 2) mode_desc = "Stala delta czasu (+/-)";
        if (active_date_mode == 3) mode_desc = "Interpolacja liniowa i organic jitter";
        if (active_date_mode == 4) mode_desc = "Godziny robocze (09:00 - 17:00)";
        if (active_date_mode == 5) mode_desc = "Synchronizacja sygnatur i auto-heal";

        auto date_summary = DateModals::render_strategy_summary(
            active_date_mode, mode_desc, sparkline_str, current_seed
        );

        // 4. Nagłówek
        std::string branch_name = dag->head().branch_ref.empty() ? "detached" : dag->head().branch_ref;
        auto header = hbox({
            text(" GitData X v4.1 ") | color(Theme2026::Emerald()) | bold,
            text("── repo: " + repo.git_dir().string() + " ── branch: " + branch_name + " ──") | color(Theme2026::TextMuted()),
            filler(),
            text(" DAG: OK ") | color(Theme2026::BackgroundDark()) | bgcolor(Theme2026::Emerald()) | bold
        });

        // 5. Pasek skrótów klawiszowych
        auto footer_hints = hbox({
            text(" [Space] Zaznacz ") | color(Theme2026::TextPrimary()) | bold,
            text("[1-6] Tryb Daty ") | color(Theme2026::TextPrimary()),
            text("[s] Rozdziel (1->K) ") | color(Theme2026::NeonViolet()) | bold,
            text("[R] Przelosuj ") | color(Theme2026::Emerald()),
            text("[p] Zapisz ") | color(Theme2026::Amber()) | bold,
            text("[u] Undo ") | color(Theme2026::Coral()) | bold,
            text("[q] Wyjdz ") | color(Theme2026::TextMuted()),
            filler(),
            text("Status: " + status_notification + " ") | color(Theme2026::Emerald())
        }) | bgcolor(Theme2026::BackgroundLight());

        return window(
            header,
            vbox({
                hbox({
                    window(text(" Historia Commitow (Graf DAG) ") | color(Theme2026::TextPrimary()), dag_panel) | flex,
                    window(text(" Kreator Rozdzielania (1 -> K) ") | color(Theme2026::NeonViolet()), split_panel)
                }) | flex,
                date_summary,
                footer_hints
            })
        ) | bgcolor(Theme2026::BackgroundDark());
    });

    // Obsługa zdarzeń klawiatury
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }

        // Nawigacja góra / dół
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            if (selected_index > 0) --selected_index;
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (!sorted_commits.empty() && selected_index < sorted_commits.size() - 1) {
                ++selected_index;
            }
            return true;
        }

        // [R]: Przelosowanie seeda
        if (event == Event::Character('R') || event == Event::Character('r')) {
            current_seed = (current_seed * 6364136223846793005ULL) + 1442695040888963407ULL;
            status_notification = "Przelosowano uklad losowy";
            return true;
        }

        // [1-6]: Wybór trybu manipulacji czasem
        if (event.is_character() && event.character()[0] >= '1' && event.character()[0] <= '6') {
            active_date_mode = event.character()[0] - '0';
            status_notification = "Wybrano Tryb manipulacji czasem [" + std::to_string(active_date_mode) + "]";
            return true;
        }

        // [s]: Rozbicie zaznaczonego commitu 1 -> K
        if (event == Event::Character('s')) {
            if (sorted_commits.empty()) return true;
            const git::Oid cur_id = sorted_commits[selected_index];
            const core::CommitNode* node = dag->find(cur_id);
            if (node == nullptr || node->is_merge()) {
                status_notification = "Blad: Nie mozna rozbijac merge commitu";
                return true;
            }

            git::InMemorySplitter splitter(repo);
            git::Oid p_tree = node->parents.empty()
                                  ? splitter.empty_tree_oid().value()
                                  : dag->find(node->parents[0])->tree_id;
            const auto deltas = splitter.diff_trees(p_tree, node->tree_id);
            if (!deltas.has_value() || deltas->empty()) {
                status_notification = "Blad: Brak plikow do rozbicia w wybranym commicie";
                return true;
            }

            auto plan_res = core::CommitSplitter::plan_file_split(*node, *deltas, split_K);
            if (plan_res.has_value()) {
                planner->set_split_action(cur_id, *plan_res);
                status_notification = "Zaplanowano rozbicie 1 -> " + std::to_string(plan_res->K);
            } else {
                status_notification = "Blad planowania rozbicia: " + plan_res.error();
            }
            return true;
        }

        // [p]: Plan & Zapis (Wykonanie w ODB)
        if (event == Event::Character('p')) {
            core::RewriteEngine engine(repo, *dag);
            auto report = engine.execute(*planner);
            if (report.has_value()) {
                status_notification = "Sukces! Zapisano commit w ODB i zaktualizowano HEAD";
                // Przeładowujemy graf
                if (auto new_dag = git::load_dag_snapshot(repo)) {
                    *dag = std::move(*new_dag);
                    sorted_commits = dag->topological_sort_oldest_first().value_or(std::vector<git::Oid>{});
                    *planner = core::RewritePlanner(*dag);
                }
            } else {
                status_notification = "Blad zapisu: " + report.error().message;
            }
            return true;
        }

        // [u]: Undo
        if (event == Event::Character('u')) {
            core::BackupManager backup_mgr(repo);
            auto undo_res = backup_mgr.restore_latest();
            if (undo_res.has_value()) {
                status_notification = "Cofnieto zmiany (Undo zakonczone sukcesem)";
                if (auto new_dag = git::load_dag_snapshot(repo)) {
                    *dag = std::move(*new_dag);
                    sorted_commits = dag->topological_sort_oldest_first().value_or(std::vector<git::Oid>{});
                    *planner = core::RewritePlanner(*dag);
                }
            } else {
                status_notification = "Blad Undo: " + undo_res.error().message;
            }
            return true;
        }

        return false;
    });

    screen.Loop(component);
    return 0;
}

}  // namespace gitdatax::tui
