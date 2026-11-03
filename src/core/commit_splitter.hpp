#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/commit_node.hpp"
#include "git/in_memory_splitter.hpp"
#include "git/oid.hpp"

namespace gitdatax::core {

// Pojedynczy krok rozbicia commitu (1 -> K)
struct SplitStep {
    std::size_t step_index{0};                  // 0 .. K-1
    std::vector<git::FileDelta> completed_files;// Pliki finalizowane w tym kroku
    std::vector<git::HunkDelta> partial_hunks;  // Cząstkowe bloki zmian
    CommitSignature author;
    CommitSignature committer;
    std::string message;                        // Opis nowego commitu
};

// Kompletny plan dekompozycji jednego commitu na K mniejszych
struct SplitPlan {
    git::Oid original_commit_id;
    std::size_t K{2};
    std::vector<SplitStep> steps;
};

// Tryb automatycznego podziału plików
enum class SplitDistributionMode {
    Uniform,                    // Równomierne rozłożenie (~F / K plików na krok)
    SemanticHeadersSourceTests  // Semantyczny: nagłówki -> implementacja -> testy i dokumentacja
};

// Moduł dekompozycji diffa 1 -> K i parowania z czasem oraz szablonami opisów (Sekcja 5).
class CommitSplitter {
public:
    // Tworzy plan dekompozycji commitu na K części na poziomie plików.
    // Ostatni krok zawsze otrzymuje dokładny czas oryginalnego commitu tc.
    static std::expected<SplitPlan, std::string> plan_file_split(
        const CommitNode& commit,
        const std::vector<git::FileDelta>& deltas,
        std::size_t K,
        SplitDistributionMode mode = SplitDistributionMode::Uniform,
        std::string_view message_template = "{orig_summary} ({k}/{K}: {primary_file})"
    );

    // Formatowanie opisu wg inteligentnego szablonu (np. "{orig_summary} ({k}/{K}: {primary_file})")
    static std::string format_step_message(
        std::string_view template_str,
        const std::string& orig_summary,
        std::size_t k_1_based,
        std::size_t total_K,
        const std::string& primary_file
    );
};

}  // namespace gitdatax::core
