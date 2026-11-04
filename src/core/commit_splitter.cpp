#include "core/commit_splitter.hpp"

#include <algorithm>
#include <string>

namespace gitdatax::core {

namespace {

int semantic_file_priority(const std::string& path) {
    auto ends_with = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    // 1. Nagłówki
    if (ends_with(path, ".h") || ends_with(path, ".hpp") || ends_with(path, ".hxx")) {
        return 1;
    }
    // 3. Testy i dokumentacja
    if (path.find("test") != std::string::npos || path.find("Test") != std::string::npos ||
        ends_with(path, ".md") || ends_with(path, ".txt") || ends_with(path, ".rst")) {
        return 3;
    }
    // 2. Implementacja kodu źródłowego
    return 2;
}

void replace_all(std::string& str, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    std::size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

}  // namespace

std::string CommitSplitter::format_step_message(
    std::string_view template_str,
    const std::string& orig_summary,
    std::size_t k_1_based,
    std::size_t total_K,
    const std::string& primary_file) {

    std::string formatted(template_str);
    replace_all(formatted, "{orig_summary}", orig_summary);
    replace_all(formatted, "{k}", std::to_string(k_1_based));
    replace_all(formatted, "{K}", std::to_string(total_K));
    replace_all(formatted, "{primary_file}", primary_file);
    return formatted;
}

std::expected<SplitPlan, std::string> CommitSplitter::plan_file_split(
    const CommitNode& commit,
    const std::vector<git::FileDelta>& deltas,
    std::size_t K,
    SplitDistributionMode mode,
    std::string_view message_template) {

    if (commit.is_merge()) {
        return std::unexpected("Rozbijanie merge commitu (1 -> K) jest zablokowane ze wzgledu na integralnosc DAG");
    }
    if (K < 2) {
        return std::unexpected("Liczba commitow K musi byc >= 2");
    }
    if (deltas.empty()) {
        return std::unexpected("Commit nie zawiera zadnych zmodyfikowanych plikow do rozbicia");
    }

    // Jeśli K jest większe niż liczba plików, ograniczamy K do liczby plików
    const std::size_t effective_K = std::min(K, deltas.size());

    std::vector<git::FileDelta> sorted_deltas = deltas;
    if (mode == SplitDistributionMode::SemanticHeadersSourceTests) {
        std::stable_sort(sorted_deltas.begin(), sorted_deltas.end(),
                         [](const git::FileDelta& a, const git::FileDelta& b) {
                             return semantic_file_priority(a.path) < semantic_file_priority(b.path);
                         });
    }

    const std::int64_t t_commit = commit.committer.timestamp;
    // Czas rodzica (lub fallback dla commitu korzenia)
    const std::int64_t t_parent = !commit.parents.empty()
                                      ? (t_commit - static_cast<std::int64_t>(effective_K) * 1800)
                                      : (t_commit - static_cast<std::int64_t>(effective_K) * 3600);

    SplitPlan plan;
    plan.original_commit_id = commit.id;
    plan.K = effective_K;
    plan.steps.reserve(effective_K);

    for (std::size_t i = 0; i < effective_K; ++i) {
        const std::size_t start_idx = (i * sorted_deltas.size()) / effective_K;
        const std::size_t end_idx = ((i + 1) * sorted_deltas.size()) / effective_K;

        std::vector<git::FileDelta> step_files;
        step_files.reserve(end_idx - start_idx);
        for (std::size_t idx = start_idx; idx < end_idx; ++idx) {
            step_files.push_back(sorted_deltas[idx]);
        }

        const std::string primary = step_files.empty() ? "" : step_files.front().path;
        const std::string step_msg = format_step_message(message_template, commit.summary, i + 1, effective_K, primary);

        // Wyznaczenie czasu: ostatni krok otrzymuje dokładnie oryginalny czas t_commit
        std::int64_t step_time = 0;
        if (i == effective_K - 1) {
            step_time = t_commit;
        } else {
            const double fraction = static_cast<double>(i + 1) / static_cast<double>(effective_K + 1);
            step_time = t_parent + static_cast<std::int64_t>(fraction * static_cast<double>(t_commit - t_parent));
        }

        CommitSignature author = commit.author;
        author.timestamp = step_time;

        CommitSignature committer = commit.committer;
        committer.timestamp = step_time;

        plan.steps.push_back(SplitStep{
            .step_index = i,
            .completed_files = std::move(step_files),
            .partial_hunks = {},
            .author = author,
            .committer = committer,
            .message = step_msg + "\n"
        });
    }

    return plan;
}

}  // namespace gitdatax::core
