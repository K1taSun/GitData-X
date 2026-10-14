#include "core/time_strategies.hpp"

#include <algorithm>

namespace gitdatax::core {

namespace {

// Pomocnik do selektywnej aktualizacji authora, committera lub obu.
void update_commit_dates(CommitDatePair& pair, const TimePoint& new_time, DateTarget target) {
    if (target == DateTarget::AuthorOnly || target == DateTarget::Both) {
        pair.author = new_time;
    }
    if (target == DateTarget::CommitterOnly || target == DateTarget::Both) {
        pair.committer = new_time;
    }
}

}  // namespace

std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_absolute(
    const std::vector<CommitTimeInput>& commits,
    const TimePoint& target_time,
    DateTarget target) {
    std::vector<CommitDatePair> result;
    result.reserve(commits.size());

    for (const auto& commit : commits) {
        CommitDatePair pair = commit.dates;
        update_commit_dates(pair, target_time, target);
        result.push_back(pair);
    }
    return result;
}

std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_parent_relative(
    const std::vector<CommitTimeInput>& commits,
    std::int64_t offset_after_parent_sec,
    DateTarget target) {
    std::vector<CommitDatePair> result;
    result.reserve(commits.size());

    for (const auto& commit : commits) {
        CommitDatePair pair = commit.dates;

        TimePoint base_time;
        if (!commit.parent_dates.empty()) {
            // Używamy pierwszego rodzica (first-parent) jako bazy chronologicznej.
            base_time = commit.parent_dates.front();
        } else {
            // Dla korzenia bazą jest jego obecny czas.
            base_time = commit.dates.committer;
        }

        const TimePoint new_time{
            .timestamp = base_time.timestamp + offset_after_parent_sec,
            .tz_offset_min = base_time.tz_offset_min
        };

        update_commit_dates(pair, new_time, target);
        result.push_back(pair);
    }
    return result;
}

std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_delta_shift(
    const std::vector<CommitTimeInput>& commits,
    std::int64_t delta_sec,
    DateTarget target) {
    std::vector<CommitDatePair> result;
    result.reserve(commits.size());

    for (const auto& commit : commits) {
        CommitDatePair pair = commit.dates;

        if (target == DateTarget::AuthorOnly || target == DateTarget::Both) {
            pair.author.timestamp += delta_sec;
        }
        if (target == DateTarget::CommitterOnly || target == DateTarget::Both) {
            pair.committer.timestamp += delta_sec;
        }
        result.push_back(pair);
    }
    return result;
}

std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_range_jitter(
    const std::vector<CommitTimeInput>& commits,
    const RangeJitterConfig& config) {
    const std::size_t n = commits.size();
    if (n == 0) {
        return std::vector<CommitDatePair>{};
    }

    // Walidacja: Tend musi pomieścić N commitów z minimalnym krokiem 1 sekundy.
    const std::int64_t min_span = static_cast<std::int64_t>(n - 1);
    if (config.t_end < config.t_start + min_span) {
        return std::unexpected(
            "Tend (" + std::to_string(config.t_end) + ") musi byc >= Tstart (" +
            std::to_string(config.t_start) + ") + (N - 1) * 1s (wymagane " +
            std::to_string(config.t_start + min_span) + ")");
    }

    std::mt19937_64 rng(config.seed);
    std::vector<std::int64_t> generated_timestamps(n);

    if (n == 1) {
        // Specyfikacja strona 7:
        // Dla N = 1: t'0 = Tstart + eps_0, eps_0 ~ U(0, Tend - Tstart)
        const std::int64_t span = config.t_end - config.t_start;
        if (span == 0) {
            generated_timestamps[0] = config.t_start;
        } else {
            std::uniform_int_distribution<std::int64_t> dist(0, span);
            generated_timestamps[0] = config.t_start + dist(rng);
        }
    } else {
        // Specyfikacja strona 7:
        // Dla N > 1: t'i = Tstart + [ i / (N - 1) ] * (Tend - Tstart) + eps_i
        // Gwarancja ścisłej monotoniczności: sigma < (Tend - Tstart) / [2(N - 1)]
        const std::int64_t total_span = config.t_end - config.t_start;
        const std::int64_t segments = static_cast<std::int64_t>(n - 1);

        // Maksymalna dozwolona sigma wykluczająca nakładanie się przedziałów sasiednich punktów
        std::int64_t max_sigma = (total_span / (2 * segments)) - 1;
        if (max_sigma < 0) {
            max_sigma = 0;
        }

        std::int64_t effective_sigma = max_sigma;
        if (config.max_jitter_sec.has_value()) {
            effective_sigma = std::min(*config.max_jitter_sec, max_sigma);
        }

        std::uniform_int_distribution<std::int64_t> jitter_dist(-effective_sigma, effective_sigma);

        for (std::size_t i = 0; i < n; ++i) {
            const std::int64_t base = config.t_start + (static_cast<std::int64_t>(i) * total_span) / segments;
            const std::int64_t jitter = (effective_sigma > 0) ? jitter_dist(rng) : 0;
            generated_timestamps[i] = base + jitter;
        }

        // Zapewnienie t'0 >= Tstart oraz t'_{N-1} <= Tend
        generated_timestamps.front() = std::max(generated_timestamps.front(), config.t_start);
        generated_timestamps.back() = std::min(generated_timestamps.back(), config.t_end);

        // Wymuszenie ścisłej monotoniczności: t'_i >= t'_{i-1} + 1
        for (std::size_t i = 1; i < n; ++i) {
            if (generated_timestamps[i] <= generated_timestamps[i - 1]) {
                generated_timestamps[i] = generated_timestamps[i - 1] + 1;
            }
        }

        // Korekta wsteczna na wypadek przekroczenia Tend
        if (generated_timestamps.back() > config.t_end) {
            generated_timestamps.back() = config.t_end;
            for (std::size_t i = n - 1; i > 0; --i) {
                if (generated_timestamps[i - 1] >= generated_timestamps[i]) {
                    generated_timestamps[i - 1] = generated_timestamps[i] - 1;
                }
            }
        }
    }

    std::vector<CommitDatePair> result;
    result.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        CommitDatePair pair = commits[i].dates;
        const int tz_author = config.new_tz_offset_min.value_or(pair.author.tz_offset_min);
        const int tz_committer = config.new_tz_offset_min.value_or(pair.committer.tz_offset_min);

        const TimePoint new_tp{
            .timestamp = generated_timestamps[i],
            .tz_offset_min = (config.target == DateTarget::AuthorOnly) ? tz_author : tz_committer
        };

        update_commit_dates(pair, new_tp, config.target);
        result.push_back(pair);
    }

    return result;
}

}  // namespace gitdatax::core
