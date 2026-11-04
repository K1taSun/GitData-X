#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace gitdatax::core {

// Struktura przechowująca punkt w czasie z jawną strefą czasową.
// Używamy int64_t na timestamp, aby uniknąć problemu roku 2038 (Y2038 bug).
struct TimePoint {
    std::int64_t timestamp{0}; // Liczba sekund od epoki Unix w czasie UTC.
    int tz_offset_min{0};      // Przesunięcie strefy w minutach (np. +60 to UTC+1, -300 to UTC-5).

    // C++20 operator domyślnego porównania (równość pól składowych).
    bool operator==(const TimePoint&) const = default;
};

// Struktura rozbicia kalendarzowego na czytelne składowe (data i czas lokalny).
struct DateTimeCivil {
    int year{1970};
    int month{1};       // 1 - 12
    int day{1};         // 1 - 31
    int hour{0};        // 0 - 23
    int minute{0};      // 0 - 59
    int second{0};      // 0 - 59
    int weekday{0};     // 1 = Poniedziałek, ..., 7 = Niedziela (zgodnie z ISO-8601)
};

// Konwertuje timestamp UTC oraz offset strefy na lokalne składowe kalendarzowe.
DateTimeCivil to_civil(std::int64_t timestamp_utc, int tz_offset_min);

// Konwertuje lokalne składowe kalendarzowe i offset strefy na uniwersalny timestamp UTC.
std::int64_t from_civil(const DateTimeCivil& civil, int tz_offset_min);

// Zwraca true jeśli dany rok w kalendarzu gregoriańskim jest przestępny.
constexpr bool is_leap_year(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Zwraca true jeśli data w lokalnej strefie wypada w weekend (Sobota lub Niedziela).
bool is_weekend(std::int64_t timestamp_utc, int tz_offset_min);

// Parsowanie formatu ISO-8601 (np. "2026-11-10 18:30:00 +0100" lub "2026-11-10T18:30:00Z").
// std::string_view pozwala na odczyt tekstu bez zbędnej alokacji pamięci na stercie.
std::expected<TimePoint, std::string> parse_iso8601(std::string_view text);

// Formatowanie punktu w czasie do kanonicznego formatu Gita: "YYYY-MM-DD HH:MM:SS ±HHMM".
std::string format_iso8601(const TimePoint& tp);

// Parsowanie delty czasu, np. "+2d 4h 30m", "-3h 15m 10s", "+1w".
// Zwraca różnicę w sekundach (dodatnią lub ujemną).
std::expected<std::int64_t, std::string> parse_duration_delta(std::string_view text);

}  // namespace gitdatax::core

