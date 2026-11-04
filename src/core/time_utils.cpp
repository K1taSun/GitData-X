#include "core/time_utils.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace gitdatax::core {

namespace {

// Pomocnicza funkcja do usuwania białych znaków z początku i końca string_view.
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace

DateTimeCivil to_civil(std::int64_t timestamp_utc, int tz_offset_min) {
    // Uwzględniamy przesunięcie strefy: dodajemy minuty strefy przeliczone na sekundy.
    const std::int64_t local_sec = timestamp_utc + static_cast<std::int64_t>(tz_offset_min) * 60;

    std::int64_t days = local_sec / 86400;
    std::int64_t rem_sec = local_sec % 86400;
    if (rem_sec < 0) {
        rem_sec += 86400;
        days -= 1;
    }

    const int hour = static_cast<int>(rem_sec / 3600);
    const int minute = static_cast<int>((rem_sec % 3600) / 60);
    const int second = static_cast<int>(rem_sec % 60);

    // Algorytm kalendarza cywilnego (Howard Hinnant, C++20 chrono)
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1024 + doe / 1461 - doe / 142401) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int year = static_cast<int>(y + (m <= 2));

    // Obliczenie dnia tygodnia: 1970-01-01 to czwartek (4).
    // ISO-8601: 1 = Poniedziałek, ..., 7 = Niedziela.
    const int weekday = static_cast<int>((days - 719468 + 3) % 7);
    const int iso_weekday = (weekday < 0 ? weekday + 7 : weekday) + 1;

    return DateTimeCivil{
        .year = year,
        .month = static_cast<int>(m),
        .day = static_cast<int>(d),
        .hour = hour,
        .minute = minute,
        .second = second,
        .weekday = iso_weekday
    };
}

std::int64_t from_civil(const DateTimeCivil& civil, int tz_offset_min) {
    int y = civil.year;
    const int m = civil.month;
    const int d = civil.day;
    y -= (m <= 2) ? 1 : 0;

    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097 + static_cast<std::int64_t>(doe) - 719468;

    const std::int64_t local_sec = days * 86400 +
                                  static_cast<std::int64_t>(civil.hour) * 3600 +
                                  static_cast<std::int64_t>(civil.minute) * 60 +
                                  civil.second;

    // Przeliczenie z czasu lokalnego na czas UTC.
    return local_sec - static_cast<std::int64_t>(tz_offset_min) * 60;
}

bool is_weekend(std::int64_t timestamp_utc, int tz_offset_min) {
    const DateTimeCivil civil = to_civil(timestamp_utc, tz_offset_min);
    return civil.weekday == 6 || civil.weekday == 7; // Sobota lub Niedziela
}

std::expected<TimePoint, std::string> parse_iso8601(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::unexpected("Pusty lancuch daty");
    }

    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
    int tz_sign = 1;
    int tz_h = 0, tz_m = 0;
    bool has_time = false;
    bool has_tz = false;

    // Konwersja na string do bezpiecznego skanowania sscanf_s
    std::string s_text(text);
    const char* c_str = s_text.c_str();

    // 1. Sprawdzamy pełny format: "YYYY-MM-DD HH:MM:SS +HHMM" lub z dwukropkiem w strefie
    int matched = 0;
    char sep = ' ';
    char tz_s = '+';

    if (std::sscanf(c_str, "%4d-%2d-%2d%c%2d:%2d:%2d %c%2d%2d",
                    &y, &m, &d, &sep, &h, &min, &s, &tz_s, &tz_h, &tz_m) == 10) {
        has_time = true;
        has_tz = true;
        tz_sign = (tz_s == '-') ? -1 : 1;
    } else if (std::sscanf(c_str, "%4d-%2d-%2d%c%2d:%2d:%2d%c%2d:%2d",
                           &y, &m, &d, &sep, &h, &min, &s, &tz_s, &tz_h, &tz_m) == 10) {
        has_time = true;
        has_tz = true;
        tz_sign = (tz_s == '-') ? -1 : 1;
    } else if (std::sscanf(c_str, "%4d-%2d-%2d%c%2d:%2d:%2dZ",
                           &y, &m, &d, &sep, &h, &min, &s) == 7) {
        has_time = true;
        has_tz = true;
        tz_sign = 1;
        tz_h = 0;
        tz_m = 0;
    } else if (std::sscanf(c_str, "%4d-%2d-%2d%c%2d:%2d:%2d",
                           &y, &m, &d, &sep, &h, &min, &s) == 7) {
        has_time = true;
        has_tz = false;
    } else if (std::sscanf(c_str, "%4d-%2d-%2d", &y, &m, &d) == 3) {
        has_time = false;
        has_tz = false;
    } else {
        return std::unexpected("Niepoprawny format ISO-8601: " + std::string(text));
    }

    if (m < 1 || m > 12 || d < 1 || d > 31 || h < 0 || h > 23 || min < 0 || min > 59 || s < 0 || s > 60) {
        return std::unexpected("Niepoprawne wartosci liczbowe daty/czasu: " + std::string(text));
    }

    const int total_tz_offset = has_tz ? tz_sign * (tz_h * 60 + tz_m) : 0;
    const DateTimeCivil civil{
        .year = y, .month = m, .day = d,
        .hour = h, .minute = min, .second = s
    };
    const std::int64_t utc_ts = from_civil(civil, total_tz_offset);

    return TimePoint{
        .timestamp = utc_ts,
        .tz_offset_min = total_tz_offset
    };
}

std::string format_iso8601(const TimePoint& tp) {
    const DateTimeCivil civil = to_civil(tp.timestamp, tp.tz_offset_min);

    char sign = '+';
    int offset = tp.tz_offset_min;
    if (offset < 0) {
        sign = '-';
        offset = -offset;
    }
    const int tz_h = offset / 60;
    const int tz_m = offset % 60;

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d %c%02d%02d",
                  civil.year, civil.month, civil.day,
                  civil.hour, civil.minute, civil.second,
                  sign, tz_h, tz_m);
    return std::string(buffer);
}

std::expected<std::int64_t, std::string> parse_duration_delta(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::unexpected("Pusta wartosc delty czasu");
    }

    std::int64_t sign = 1;
    if (text.front() == '+') {
        text.remove_prefix(1);
    } else if (text.front() == '-') {
        sign = -1;
        text.remove_prefix(1);
    }

    text = trim(text);
    if (text.empty()) {
        return std::unexpected("Brak jednostek po znaku w delcie");
    }

    std::int64_t total_seconds = 0;
    bool parsed_any = false;

    while (!text.empty()) {
        text = trim(text);
        if (text.empty()) break;

        // Czytamy liczbę
        std::size_t idx = 0;
        while (idx < text.size() && std::isdigit(static_cast<unsigned char>(text[idx]))) {
            ++idx;
        }

        if (idx == 0) {
            return std::unexpected("Oczekiwano liczby w delcie czasu: " + std::string(text));
        }

        std::int64_t value = 0;
        const auto res = std::from_chars(text.data(), text.data() + idx, value);
        if (res.ec != std::errc{}) {
            return std::unexpected("Blad parsowania liczby w delcie");
        }

        text.remove_prefix(idx);
        text = trim(text);

        if (text.empty()) {
            return std::unexpected("Brak jednostki czasu (w/d/h/m/s)");
        }

        const char unit = text.front();
        text.remove_prefix(1);

        std::int64_t multiplier = 0;
        switch (unit) {
            case 'w': multiplier = 7 * 86400; break;
            case 'd': multiplier = 86400; break;
            case 'h': multiplier = 3600; break;
            case 'm': multiplier = 60; break;
            case 's': multiplier = 1; break;
            default:
                return std::unexpected(std::string("Nieznana jednostka czasu: '") + unit + "' (oczekiwano w, d, h, m lub s)");
        }

        total_seconds += value * multiplier;
        parsed_any = true;
    }

    if (!parsed_any) {
        return std::unexpected("Nie znaleziono zadnych poprawnych jednostek w delcie");
    }

    return sign * total_seconds;
}

}  // namespace gitdatax::core

