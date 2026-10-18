#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/time_utils.hpp"
#include "git/oid.hpp"

namespace gitdatax::core {

// Określa które pola sygnatur podlegają modyfikacji.
enum class DateTarget {
    AuthorOnly,
    CommitterOnly,
    Both
};

// Para znaczników czasu dla pojedynczego commitu.
struct CommitDatePair {
    TimePoint author;
    TimePoint committer;

    bool operator==(const CommitDatePair&) const = default;
};

// Wejście dla silnika transformacji czasu zawierające identyfikator, obecne daty
// oraz daty rodziców (niezbędne dla relacji przyczynowo-skutkowych w DAG).
struct CommitTimeInput {
    git::Oid id;
    CommitDatePair dates;
    std::vector<TimePoint> parent_dates; // Główna data rodzica (np. committer timestamp)
};

// --- SPOSÓB 1: Data bezwzględna lub ze wzorca względnego ---

// Ustawia sztywną datę bezwzględną (np. 2026-11-10 18:30:00 +0100).
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_absolute(
    const std::vector<CommitTimeInput>& commits,
    const TimePoint& target_time,
    DateTarget target = DateTarget::Both
);

// Ustawia datę na podstawie pierwszego rodzica commitu: t' = t_parent + offset_sec.
// Dla commitów korzenia (brak rodzica) przyjmuje własną dotychczasową datę + offset_sec.
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_parent_relative(
    const std::vector<CommitTimeInput>& commits,
    std::int64_t offset_after_parent_sec,
    DateTarget target = DateTarget::Both
);

// --- SPOSÓB 2: Przesunięcie o stałą deltę czasu (Constant Delta Shift) ---

// Przesuwa commity o stałą wartość delta_sec (np. +2 dni lub -3 godziny).
// W 100% zachowuje oryginalne odstępy czasowe między commitami: t'_b - t'_a == t_b - t_a.
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_delta_shift(
    const std::vector<CommitTimeInput>& commits,
    std::int64_t delta_sec,
    DateTarget target = DateTarget::Both
);

// --- SPOSÓB 3: Interpolacja liniowa i zaszumiona w przedziale (Range & Jitter) ---

struct RangeJitterConfig {
    std::int64_t t_start{0}; // Początek przedziału w sekundach UTC
    std::int64_t t_end{0};   // Koniec przedziału w sekundach UTC
    std::uint64_t seed{1337};
    std::optional<std::int64_t> max_jitter_sec{std::nullopt}; // Opcjonalne organic jitter sigma
    std::optional<int> new_tz_offset_min{std::nullopt};       // Opcjonalna nowa strefa (jeśli nullopt, zachowaj oryginalną)
    DateTarget target{DateTarget::Both};
};

// Równomiernie rozkłada commity w przedziale [Tstart, Tend] z organicznym szumem (jitter).
// Gwarantuje ścisłą monotoniczność t'_0 < t'_1 < ... < t'_{N-1}.
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_range_jitter(
    const std::vector<CommitTimeInput>& commits,
    const RangeJitterConfig& config
);

// --- SPOSÓB 4: Mapowanie do okna godzin roboczych (Work-Hours Clamping) ---

struct WorkHoursConfig {
    int start_hour{9};                  // Domyślnie 09:00
    int start_minute{0};
    int end_hour{17};                   // Domyślnie 17:00
    int end_minute{0};
    std::int64_t step_min_sec{8 * 60};  // delta_min (np. 8m)
    std::int64_t step_max_sec{25 * 60}; // delta_max (np. 25m)
    std::uint64_t seed{1337};
    DateTarget target{DateTarget::Both};
};

// Automatycznie przenosi commity nocne i weekendowe do okna godzin roboczych (np. Pn-Pt 09:00-17:00).
// Obsługuje przepełnienie dnia (Day Overflow Protection) przenosząc nadmiarowe commity na poranek kolejnego dnia roboczego.
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_work_hours(
    const std::vector<CommitTimeInput>& commits,
    const WorkHoursConfig& config
);

}  // namespace gitdatax::core
