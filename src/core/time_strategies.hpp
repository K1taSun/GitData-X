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

// Flagi celu modyfikacji wymagane przez wspólny interfejs strategii ITimeStrategy.
enum class TimeTargetFlag {
    ModifyAuthor,
    ModifyCommitter,
    SyncBoth
};

constexpr DateTarget to_date_target(TimeTargetFlag flag) noexcept {
    switch (flag) {
        case TimeTargetFlag::ModifyAuthor: return DateTarget::AuthorOnly;
        case TimeTargetFlag::ModifyCommitter: return DateTarget::CommitterOnly;
        case TimeTargetFlag::SyncBoth: return DateTarget::Both;
    }
    return DateTarget::Both;
}

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

// Wspólny interfejs dla wszystkich 6 strategii transformacji dat commitów.
// Przyjmuje jawny generator std::mt19937_64 oraz flagę celu, co gwarantuje
// deterministyczność losowań i umożliwia jednolite wywoływanie strategii.
class ITimeStrategy {
public:
    virtual ~ITimeStrategy() = default;

    [[nodiscard]] virtual std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const = 0;
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

// --- SPOSÓB 5: Synchronizacja par sygnatur, Auto-Heal chronologii DAG i strefy ---

enum class SignatureSyncMode {
    None,
    CommitterToAuthor, // Committer = Author (usuwa ślady po amend / rebase)
    AuthorToCommitter  // Author = Committer
};

enum class TimezoneMode {
    None,
    PreserveInstantUtc, // Zachowaj bezwzględną chwilę UTC (zmienia się tylko etykieta strefy)
    PreserveClockTime   // Zachowaj godzinę zegarową (np. 14:00 pozostaje 14:00 w nowej strefie)
};

struct Strategy5Config {
    SignatureSyncMode sync_mode{SignatureSyncMode::None};
    bool auto_heal_dag_chronology{false};
    std::int64_t auto_heal_step_sec{60}; // Domyślnie krok +60s po najpóźniejszym rodzicu
    TimezoneMode timezone_mode{TimezoneMode::None};
    int new_tz_offset_min{0};
};

// Zapewnia spójność sygnatur, naprawia topologiczne anomalie czasu (dziecko starsze od rodzica)
// oraz umożliwia konwersję strefy czasowej.
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_sync_and_heal(
    const std::vector<CommitTimeInput>& commits,
    const Strategy5Config& config
);

// --- SPOSÓB 6: Odcinkowe losowe przesunięcie domykające do pełnej daty docelowej ---

// Reprezentacja pojedynczego odcinka (sesji pracy) w strategii 6
struct TimeSegment {
    std::size_t start_index{0}; // Indeks pierwszego commitu w odcinku
    std::size_t count{0};       // Liczba commitów w odcinku (L_m)
    std::int64_t t_start{0};    // T_start^(m)
    std::int64_t t_target{0};   // T_target^(m)
};

struct PiecewiseRandomConfig {
    std::int64_t target_full_date{0};                 // T_target finalnej daty (ostatniego commitu)
    std::int64_t delta_min_sec{5 * 60};               // Delta_min (np. 5 min = 300s)
    std::int64_t delta_max_sec{35 * 60};              // Delta_max (np. 35 min = 2100s)
    std::int64_t auto_session_gap_sec{6 * 3600};      // Próg sesji (np. 6h = 21600s) do automatycznego podziału
    bool split_by_day{false};                         // Podział na odcinki przy zmianie dnia kalendarzowego
    std::vector<std::size_t> manual_break_indices{};  // Ręczne punkty podziału [B] z TUI
    std::vector<TimeSegment> explicit_segments{};     // Opcjonalne jawnie zdefiniowane odcinki z ramami czasowymi
    std::uint64_t seed{1337};
    std::optional<int> new_tz_offset_min{std::nullopt};
    DateTarget target{DateTarget::Both};
};

// Dzieli commity na M odcinków i przesuwa commity za pomocą losowego kroku (Random Walk)
// tak, aby ostatni commit odcinka (oraz finalnie cały ciąg) domknął się DOKŁADNIE
// co do sekundy we wskazanej pełnej dacie docelowej T_target.
std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_piecewise_random_to_target(
    const std::vector<CommitTimeInput>& commits,
    const PiecewiseRandomConfig& config
);

// --- Obiektowe wrappery implementujące wspólny interfejs ITimeStrategy ---

class AbsolutePresetStrategy final : public ITimeStrategy {
public:
    explicit AbsolutePresetStrategy(const TimePoint& new_time);
    explicit AbsolutePresetStrategy(const ParsedPreset& preset);

    [[nodiscard]] std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const override;

private:
    ParsedPreset preset_;
};

class ConstantDeltaShiftStrategy final : public ITimeStrategy {
public:
    explicit ConstantDeltaShiftStrategy(std::int64_t delta_seconds);

    [[nodiscard]] std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const override;

private:
    std::int64_t delta_seconds_{0};
};

class RangeJitterStrategy final : public ITimeStrategy {
public:
    explicit RangeJitterStrategy(RangeJitterConfig config);

    [[nodiscard]] std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const override;

private:
    RangeJitterConfig config_;
};

class WorkHoursStrategy final : public ITimeStrategy {
public:
    explicit WorkHoursStrategy(WorkHoursConfig config);

    [[nodiscard]] std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const override;

private:
    WorkHoursConfig config_;
};

class SyncAndHealStrategy final : public ITimeStrategy {
public:
    explicit SyncAndHealStrategy(Strategy5Config config);

    [[nodiscard]] std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const override;

private:
    Strategy5Config config_;
};

class PiecewiseRandomStrategy final : public ITimeStrategy {
public:
    explicit PiecewiseRandomStrategy(PiecewiseRandomConfig config);

    [[nodiscard]] std::expected<std::vector<CommitDatePair>, std::string> apply(
        const std::vector<CommitTimeInput>& commits,
        std::mt19937_64& rng,
        TimeTargetFlag target = TimeTargetFlag::SyncBoth
    ) const override;

private:
    PiecewiseRandomConfig config_;
};

}  // namespace gitdatax::core
