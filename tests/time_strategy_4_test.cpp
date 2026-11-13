#include <catch2/catch_test_macros.hpp>

#include "core/time_strategies.hpp"
#include "core/time_utils.hpp"

using namespace gitdatax::core;

namespace {

CommitTimeInput make_commit_at(int year, int month, int day, int hour, int minute, int second = 0, int tz = 60) {
    const DateTimeCivil civil{
        .year = year, .month = month, .day = day,
        .hour = hour, .minute = minute, .second = second
    };
    const std::int64_t ts = from_civil(civil, tz);
    CommitTimeInput input;
    input.dates.author = TimePoint{.timestamp = ts, .tz_offset_min = tz};
    input.dates.committer = TimePoint{.timestamp = ts, .tz_offset_min = tz};
    return input;
}

}  // namespace

TEST_CASE("Sposob 4: Rzutowanie commitow nocnych i weekendowych", "[core][time_strategy][work_hours]") {
    WorkHoursConfig cfg{
        .start_hour = 9,
        .start_minute = 0,
        .end_hour = 17,
        .end_minute = 0,
        .step_min_sec = 600, // 10 min
        .step_max_sec = 1200, // 20 min
        .seed = 42
    };

    SECTION("Commit w srodku nocy (02:00 w srode) trafia na 09:00 w ten sam dzien") {
        // 2026-11-11 to środa
        const auto input = make_commit_at(2026, 11, 11, 2, 0);
        const auto res = apply_strategy_work_hours({input}, cfg);
        REQUIRE(res.has_value());

        const auto civil = to_civil((*res)[0].committer.timestamp, 60);
        REQUIRE(civil.year == 2026);
        REQUIRE(civil.month == 11);
        REQUIRE(civil.day == 11);
        REQUIRE(civil.hour == 9);
        REQUIRE(civil.minute == 0);
    }

    SECTION("Commit w sobote trafia na poniedzialek rano") {
        // 2026-11-14 to sobota
        const auto input = make_commit_at(2026, 11, 14, 14, 0);
        const auto res = apply_strategy_work_hours({input}, cfg);
        REQUIRE(res.has_value());

        // 2026-11-16 to poniedziałek
        const auto civil = to_civil((*res)[0].committer.timestamp, 60);
        REQUIRE(civil.year == 2026);
        REQUIRE(civil.month == 11);
        REQUIRE(civil.day == 16);
        REQUIRE(civil.weekday == 1); // Poniedziałek
        REQUIRE(civil.hour == 9);
    }

    SECTION("Commit w niedziele trafia na poniedzialek rano") {
        // 2026-11-15 to niedziela
        const auto input = make_commit_at(2026, 11, 15, 20, 0);
        const auto res = apply_strategy_work_hours({input}, cfg);
        REQUIRE(res.has_value());

        const auto civil = to_civil((*res)[0].committer.timestamp, 60);
        REQUIRE(civil.year == 2026);
        REQUIRE(civil.month == 11);
        REQUIRE(civil.day == 16);
        REQUIRE(civil.weekday == 1);
        REQUIRE(civil.hour == 9);
    }
}

TEST_CASE("Sposob 4: Day Overflow Protection", "[core][time_strategy][work_hours]") {
    WorkHoursConfig cfg{
        .start_hour = 9,
        .start_minute = 0,
        .end_hour = 17,
        .end_minute = 0,
        .step_min_sec = 1800, // 30 min
        .step_max_sec = 1800, // stały krok 30 min
        .seed = 123
    };

    // 2026-11-13 to piątek, commit o 16:45
    std::vector<CommitTimeInput> commits = {
        make_commit_at(2026, 11, 13, 16, 45),
        make_commit_at(2026, 11, 13, 16, 50),
        make_commit_at(2026, 11, 13, 16, 55)
    };

    const auto res = apply_strategy_work_hours(commits, cfg);
    REQUIRE(res.has_value());
    REQUIRE(res->size() == 3);

    // Commit 0 jest w piątek o 16:45 (mieści się przed 17:00)
    const auto c0 = to_civil((*res)[0].committer.timestamp, 60);
    REQUIRE(c0.weekday == 5); // Piątek
    REQUIRE(c0.hour == 16);
    REQUIRE(c0.minute == 45);

    // Commit 1 przepełnia piątek (16:45 + 30m = 17:15 > 17:00), więc trafia na poniedziałek rano!
    const auto c1 = to_civil((*res)[1].committer.timestamp, 60);
    REQUIRE(c1.weekday == 1); // Poniedziałek!
    REQUIRE(c1.hour >= 9);
    REQUIRE(c1.hour < 10);

    // Chronologia ściśle zachowana
    REQUIRE((*res)[1].committer.timestamp > (*res)[0].committer.timestamp);
    REQUIRE((*res)[2].committer.timestamp > (*res)[1].committer.timestamp);
}

TEST_CASE("Sposob 4: Walidacja niepoprawnej konfiguracji", "[core][time_strategy][work_hours]") {
    WorkHoursConfig bad_hours{
        .start_hour = 17,
        .end_hour = 9 // start > end
    };
    REQUIRE_FALSE(apply_strategy_work_hours({make_commit_at(2026, 11, 11, 12, 0)}, bad_hours).has_value());

    WorkHoursConfig bad_step{
        .step_min_sec = 0
    };
    REQUIRE_FALSE(apply_strategy_work_hours({make_commit_at(2026, 11, 11, 12, 0)}, bad_step).has_value());
}

TEST_CASE("Sposob 4: Przeniesienie dokladnego nadmiaru dnia (Day Overflow Excess)", "[core][time_strategy][work_hours]") {
    // Piątek 2026-11-13 16:45 -> +30 min daje 17:15 -> nadmiar 15 min po 17:00 trafia na poniedziałek 09:15!
    const std::vector<CommitTimeInput> commits = {
        make_commit_at(2026, 11, 13, 16, 45, 0, 60),
        make_commit_at(2026, 11, 13, 16, 50, 0, 60)
    };

    const WorkHoursStrategy strategy(WorkHoursConfig{
        .start_hour = 9,
        .start_minute = 0,
        .end_hour = 17,
        .end_minute = 0,
        .step_min_sec = 30 * 60,
        .step_max_sec = 30 * 60,
        .seed = 100
    });
    const ITimeStrategy& iface = strategy;
    std::mt19937_64 rng(100);

    const auto res = iface.apply(commits, rng, TimeTargetFlag::ModifyAuthor);
    REQUIRE(res.has_value());
    REQUIRE(res->size() == 2);

    const auto c1 = to_civil((*res)[1].author.timestamp, 60);
    REQUIRE(c1.year == 2026);
    REQUIRE(c1.month == 11);
    REQUIRE(c1.day == 16); // Poniedziałek
    REQUIRE(c1.hour == 9);
    REQUIRE(c1.minute == 15);
    // Committer pozostał nietknięty przy fladze ModifyAuthor
    REQUIRE((*res)[1].committer == commits[1].dates.committer);
}

TEST_CASE("Sposob 4: Przejscie przez 29 lutego w roku przestepnym", "[core][time_strategy][work_hours]") {
    // 2024-02-28 to środa, 2024-02-29 to czwartek, 2024-03-01 to piątek.
    const std::vector<CommitTimeInput> commits = {
        make_commit_at(2024, 2, 28, 16, 50, 0, 60),
        make_commit_at(2024, 2, 28, 16, 55, 0, 60)
    };

    WorkHoursConfig cfg{
        .start_hour = 9,
        .start_minute = 0,
        .end_hour = 17,
        .end_minute = 0,
        .step_min_sec = 20 * 60,
        .step_max_sec = 20 * 60
    };

    const auto res = apply_strategy_work_hours(commits, cfg);
    REQUIRE(res.has_value());
    const auto c1 = to_civil((*res)[1].committer.timestamp, 60);
    REQUIRE(c1.year == 2024);
    REQUIRE(c1.month == 2);
    REQUIRE(c1.day == 29);
    REQUIRE(c1.hour == 9);
    REQUIRE(c1.minute == 10);
}

