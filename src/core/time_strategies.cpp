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

namespace {

// Pomocnik sprawdzający czy dany czas lokalny mieści się w zdefiniowanym oknie godzin roboczych.
bool is_within_work_window(const DateTimeCivil& civil, int start_min, int end_min) {
    if (civil.weekday == 6 || civil.weekday == 7) {
        return false; // Weekend
    }
    const int minute_of_day = civil.hour * 60 + civil.minute;
    return minute_of_day >= start_min && minute_of_day < end_min;
}

// Zwraca timestamp początku najbliższego dnia roboczego o godzinie start_hour:start_minute.
std::int64_t next_work_day_start(const DateTimeCivil& from_civil_date, int start_hour, int start_minute, int tz_offset_min) {
    DateTimeCivil next = from_civil_date;
    next.hour = start_hour;
    next.minute = start_minute;
    next.second = 0;

    // Przechodzimy do następnego dnia kalendarzowego
    // Obliczamy timestamp obecnego dnia o godz. 00:00 i dodajemy 1 dzień (86400s)
    std::int64_t next_day_ts = from_civil(DateTimeCivil{
        .year = next.year, .month = next.month, .day = next.day,
        .hour = 0, .minute = 0, .second = 0
    }, tz_offset_min) + 86400;

    DateTimeCivil next_day_civil = to_civil(next_day_ts, tz_offset_min);

    // Jeśli trafiliśmy w sobotę (6) -> przeskocz do poniedziałku (+2 dni)
    if (next_day_civil.weekday == 6) {
        next_day_ts += 2 * 86400;
        next_day_civil = to_civil(next_day_ts, tz_offset_min);
    } else if (next_day_civil.weekday == 7) { // Niedziela -> poniedziałek (+1 dzień)
        next_day_ts += 86400;
        next_day_civil = to_civil(next_day_ts, tz_offset_min);
    }

    next_day_civil.hour = start_hour;
    next_day_civil.minute = start_minute;
    next_day_civil.second = 0;
    return from_civil(next_day_civil, tz_offset_min);
}

// Rzutuje czas poza oknem na najbliższe okno robocze.
std::int64_t clamp_to_work_window(std::int64_t ts, int tz_offset_min, int start_hour, int start_minute, int end_hour, int end_minute) {
    const DateTimeCivil civil = to_civil(ts, tz_offset_min);
    const int start_min = start_hour * 60 + start_minute;
    const int end_min = end_hour * 60 + end_minute;

    if (civil.weekday == 6) {
        // Sobota -> Poniedziałek rano
        const std::int64_t mon_ts = ts + 2 * 86400;
        DateTimeCivil mon_civil = to_civil(mon_ts, tz_offset_min);
        mon_civil.hour = start_hour;
        mon_civil.minute = start_minute;
        mon_civil.second = 0;
        return from_civil(mon_civil, tz_offset_min);
    }
    if (civil.weekday == 7) {
        // Niedziela -> Poniedziałek rano
        const std::int64_t mon_ts = ts + 86400;
        DateTimeCivil mon_civil = to_civil(mon_ts, tz_offset_min);
        mon_civil.hour = start_hour;
        mon_civil.minute = start_minute;
        mon_civil.second = 0;
        return from_civil(mon_civil, tz_offset_min);
    }

    // Dzień roboczy (Pn - Pt)
    const int minute_of_day = civil.hour * 60 + civil.minute;
    if (minute_of_day < start_min) {
        // Przed rozpoczęciem pracy w tym samym dniu -> ustaw na otwarcie okna
        DateTimeCivil adjusted = civil;
        adjusted.hour = start_hour;
        adjusted.minute = start_minute;
        adjusted.second = 0;
        return from_civil(adjusted, tz_offset_min);
    }
    if (minute_of_day >= end_min) {
        // Po zakończeniu pracy -> przejdź na otwarcie kolejnego dnia roboczego
        return next_work_day_start(civil, start_hour, start_minute, tz_offset_min);
    }

    return ts;
}

}  // namespace

std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_work_hours(
    const std::vector<CommitTimeInput>& commits,
    const WorkHoursConfig& config) {
    if (commits.empty()) {
        return std::vector<CommitDatePair>{};
    }

    const int start_min = config.start_hour * 60 + config.start_minute;
    const int end_min = config.end_hour * 60 + config.end_minute;
    if (start_min >= end_min) {
        return std::unexpected("Poczatek okna roboczego musi byc wczesniejszy niz koniec");
    }
    if (config.step_min_sec <= 0 || config.step_max_sec < config.step_min_sec) {
        return std::unexpected("Niepoprawny zakres kroku delta (step_min_sec > 0 oraz step_max_sec >= step_min_sec)");
    }

    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<std::int64_t> step_dist(config.step_min_sec, config.step_max_sec);

    std::vector<CommitDatePair> result;
    result.reserve(commits.size());

    std::int64_t current_cursor = 0;

    for (std::size_t i = 0; i < commits.size(); ++i) {
        const auto& commit = commits[i];
        const TimePoint orig_tp = (config.target == DateTarget::AuthorOnly)
                                      ? commit.dates.author
                                      : commit.dates.committer;
        const int tz = orig_tp.tz_offset_min;

        if (i == 0) {
            // Pierwszy commit rzutujemy do okna roboczego
            current_cursor = clamp_to_work_window(orig_tp.timestamp, tz,
                                                  config.start_hour, config.start_minute,
                                                  config.end_hour, config.end_minute);
        } else {
            const std::int64_t step = step_dist(rng);

            // Jeśli oryginalny commit jest znacznie późniejszy niż bieżący kursor,
            // próbujemy zachować naturalny postęp czasu, o ile mieści się w oknie.
            if (orig_tp.timestamp > current_cursor + step) {
                current_cursor = clamp_to_work_window(orig_tp.timestamp, tz,
                                                      config.start_hour, config.start_minute,
                                                      config.end_hour, config.end_minute);
            } else {
                current_cursor += step;
            }

            // Day Overflow Protection: sprawdzamy czy przekroczyliśmy godzinę końcową dnia
            DateTimeCivil civil = to_civil(current_cursor, tz);
            if (!is_within_work_window(civil, start_min, end_min)) {
                // Przekroczenie okna roboczego: przenosimy na poranek kolejnego dnia roboczego
                const std::int64_t next_start = next_work_day_start(civil, config.start_hour, config.start_minute, tz);
                // Losowy mały offset w poranku nowego dnia
                const std::int64_t morning_offset = (step % (15 * 60)); // max 15 minut po otwarciu
                current_cursor = next_start + morning_offset;
            }
        }

        CommitDatePair pair = commit.dates;
        const TimePoint new_tp{.timestamp = current_cursor, .tz_offset_min = tz};
        update_commit_dates(pair, new_tp, config.target);
        result.push_back(pair);
    }

    return result;
}

std::expected<std::vector<CommitDatePair>, std::string> apply_strategy_sync_and_heal(
    const std::vector<CommitTimeInput>& commits,
    const Strategy5Config& config) {
    if (config.auto_heal_dag_chronology && config.auto_heal_step_sec <= 0) {
        return std::unexpected("Krok auto-heal musi byc scisle dodatni (> 0s)");
    }

    std::vector<CommitDatePair> result;
    result.reserve(commits.size());

    for (const auto& commit : commits) {
        CommitDatePair pair = commit.dates;

        // 1. Zmiana strefy czasowej
        if (config.timezone_mode == TimezoneMode::PreserveInstantUtc) {
            // Zachowujemy bezwzględną chwilę UTC, modyfikując jedynie etykietę strefy
            pair.author.tz_offset_min = config.new_tz_offset_min;
            pair.committer.tz_offset_min = config.new_tz_offset_min;
        } else if (config.timezone_mode == TimezoneMode::PreserveClockTime) {
            // Zachowujemy godzinę zegarową: t_new = t_old + (tz_old - tz_new) * 60
            pair.author.timestamp += static_cast<std::int64_t>(pair.author.tz_offset_min - config.new_tz_offset_min) * 60;
            pair.author.tz_offset_min = config.new_tz_offset_min;

            pair.committer.timestamp += static_cast<std::int64_t>(pair.committer.tz_offset_min - config.new_tz_offset_min) * 60;
            pair.committer.tz_offset_min = config.new_tz_offset_min;
        }

        // 2. DAG Chronology Auto-Heal: t'_i = max(parents) + step
        if (config.auto_heal_dag_chronology && !commit.parent_dates.empty()) {
            std::int64_t max_parent_time = 0;
            bool found_parent = false;

            for (const auto& p_tp : commit.parent_dates) {
                const std::int64_t pt = p_tp.timestamp;
                if (!found_parent || pt > max_parent_time) {
                    max_parent_time = pt;
                    found_parent = true;
                }
            }

            if (found_parent) {
                if (pair.committer.timestamp <= max_parent_time) {
                    pair.committer.timestamp = max_parent_time + config.auto_heal_step_sec;
                }
                if (pair.author.timestamp <= max_parent_time) {
                    pair.author.timestamp = max_parent_time + config.auto_heal_step_sec;
                }
            }
        }

        // 3. Synchronizacja par sygnatur
        if (config.sync_mode == SignatureSyncMode::CommitterToAuthor) {
            pair.committer = pair.author;
        } else if (config.sync_mode == SignatureSyncMode::AuthorToCommitter) {
            pair.author = pair.committer;
        }

        result.push_back(pair);
    }

    return result;
}

}  // namespace gitdatax::core
