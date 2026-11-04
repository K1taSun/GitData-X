#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include "core/time_utils.hpp"

using gitdatax::core::DateTimeCivil;
using gitdatax::core::from_civil;
using gitdatax::core::format_iso8601;
using gitdatax::core::is_leap_year;
using gitdatax::core::is_weekend;
using gitdatax::core::parse_duration_delta;
using gitdatax::core::parse_iso8601;
using gitdatax::core::TimePoint;
using gitdatax::core::to_civil;

TEST_CASE("Kalendarz: lata przestepne", "[core][time]") {
    REQUIRE(is_leap_year(2000));
    REQUIRE(is_leap_year(2024));
    REQUIRE(is_leap_year(2028));
    REQUIRE_FALSE(is_leap_year(1900));
    REQUIRE_FALSE(is_leap_year(2023));
    REQUIRE_FALSE(is_leap_year(2100));
}

TEST_CASE("Kalendarz: konwersja to_civil i from_civil", "[core][time]") {
    // 2026-11-10 18:30:00 UTC
    // Sprawdzamy konwersję w UTC (offset 0)
    const DateTimeCivil civil_in{
        .year = 2026, .month = 11, .day = 10,
        .hour = 18, .minute = 30, .second = 0
    };
    const std::int64_t ts = from_civil(civil_in, 0);
    const DateTimeCivil civil_out = to_civil(ts, 0);

    REQUIRE(civil_out.year == 2026);
    REQUIRE(civil_out.month == 11);
    REQUIRE(civil_out.day == 10);
    REQUIRE(civil_out.hour == 18);
    REQUIRE(civil_out.minute == 30);
    REQUIRE(civil_out.second == 0);
    // 2026-11-10 to wtorek (2)
    REQUIRE(civil_out.weekday == 2);
    REQUIRE_FALSE(is_weekend(ts, 0));
}

TEST_CASE("Kalendarz: weekendy", "[core][time]") {
    // 2026-11-14 to sobota (6), 2026-11-15 to niedziela (7)
    const DateTimeCivil sat{.year = 2026, .month = 11, .day = 14, .hour = 12, .minute = 0, .second = 0};
    const DateTimeCivil sun{.year = 2026, .month = 11, .day = 15, .hour = 12, .minute = 0, .second = 0};
    const DateTimeCivil mon{.year = 2026, .month = 11, .day = 16, .hour = 12, .minute = 0, .second = 0};

    const std::int64_t ts_sat = from_civil(sat, 0);
    const std::int64_t ts_sun = from_civil(sun, 0);
    const std::int64_t ts_mon = from_civil(mon, 0);

    REQUIRE(is_weekend(ts_sat, 0));
    REQUIRE(is_weekend(ts_sun, 0));
    REQUIRE_FALSE(is_weekend(ts_mon, 0));
}

TEST_CASE("ISO-8601: parsowanie i formatowanie", "[core][time]") {
    const auto parsed = parse_iso8601("2026-11-10 18:30:00 +0100");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->tz_offset_min == 60);

    const std::string formatted = format_iso8601(*parsed);
    REQUIRE(formatted == "2026-11-10 18:30:00 +0100");
}

TEST_CASE("ISO-8601: parsowanie alternatywnych formatow", "[core][time]") {
    const auto p1 = parse_iso8601("2026-11-10T18:30:00+01:00");
    REQUIRE(p1.has_value());
    REQUIRE(p1->tz_offset_min == 60);

    const auto p2 = parse_iso8601("2026-11-10T18:30:00Z");
    REQUIRE(p2.has_value());
    REQUIRE(p2->tz_offset_min == 0);

    const auto p3 = parse_iso8601("2026-11-10");
    REQUIRE(p3.has_value());
    const auto civil3 = to_civil(p3->timestamp, p3->tz_offset_min);
    REQUIRE(civil3.year == 2026);
    REQUIRE(civil3.month == 11);
    REQUIRE(civil3.day == 10);
    REQUIRE(civil3.hour == 0);
}

TEST_CASE("ISO-8601: bledne dane wejsciowe", "[core][time]") {
    REQUIRE_FALSE(parse_iso8601("").has_value());
    REQUIRE_FALSE(parse_iso8601("not-a-date").has_value());
    REQUIRE_FALSE(parse_iso8601("2026-13-10 12:00:00").has_value()); // miesiac 13
    REQUIRE_FALSE(parse_iso8601("2026-11-32 12:00:00").has_value()); // dzien 32
    REQUIRE_FALSE(parse_iso8601("2026-11-10 25:00:00").has_value()); // godzina 25
}

TEST_CASE("Delta: parsowanie skladni czasu", "[core][time]") {
    // +2d 4h 30m = 2*86400 + 4*3600 + 30*60 = 172800 + 14400 + 1800 = 189000
    const auto d1 = parse_duration_delta("+2d 4h 30m");
    REQUIRE(d1.has_value());
    REQUIRE(*d1 == 189000);

    // -3h 15m 10s = -(3*3600 + 15*60 + 10) = -(10800 + 900 + 10) = -11710
    const auto d2 = parse_duration_delta("-3h 15m 10s");
    REQUIRE(d2.has_value());
    REQUIRE(*d2 == -11710);

    // +1w = 7 * 86400 = 604800
    const auto d3 = parse_duration_delta("+1w");
    REQUIRE(d3.has_value());
    REQUIRE(*d3 == 604800);

    // Domyslny plus bez znaku
    const auto d4 = parse_duration_delta("45m");
    REQUIRE(d4.has_value());
    REQUIRE(*d4 == 2700);
}

TEST_CASE("Delta: bledne formaty", "[core][time]") {
    REQUIRE_FALSE(parse_duration_delta("").has_value());
    REQUIRE_FALSE(parse_duration_delta("+").has_value());
    REQUIRE_FALSE(parse_duration_delta("abc").has_value());
    REQUIRE_FALSE(parse_duration_delta("+5x").has_value());
}

TEST_CASE("Kalendarz: RapidCheck roundtrip to_civil i from_civil", "[core][time][rapidcheck]") {
    const bool passed = rc::check("from_civil(to_civil(ts, tz), tz) == ts", []() {
        // Losowy timestamp w zakresie lat 1970 - 2100
        const auto ts = *rc::gen::inRange<std::int64_t>(0, 4102444800LL);
        // Przesunięcie strefy w minutach od -720 do +840 (np. UTC-12 do UTC+14)
        const auto tz = *rc::gen::inRange<int>(-720, 840);

        const DateTimeCivil civil = to_civil(ts, tz);
        const std::int64_t recovered = from_civil(civil, tz);

        RC_ASSERT(recovered == ts);
    });
    REQUIRE(passed);
}

