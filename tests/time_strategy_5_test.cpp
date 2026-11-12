#include <catch2/catch_test_macros.hpp>

#include "core/time_strategies.hpp"
#include "core/time_utils.hpp"

using namespace gitdatax::core;

namespace {

CommitTimeInput make_input_pair(std::int64_t author_ts, int author_tz,
                                std::int64_t committer_ts, int committer_tz,
                                std::vector<TimePoint> parents = {}) {
    CommitTimeInput input;
    input.dates.author = TimePoint{.timestamp = author_ts, .tz_offset_min = author_tz};
    input.dates.committer = TimePoint{.timestamp = committer_ts, .tz_offset_min = committer_tz};
    input.parent_dates = std::move(parents);
    return input;
}

}  // namespace

TEST_CASE("Sposob 5: Synchronizacja par sygnatur", "[core][time_strategy][sync_heal]") {
    const auto input = make_input_pair(1000, 60, 2000, 120);

    SECTION("Committer = Author") {
        Strategy5Config cfg{.sync_mode = SignatureSyncMode::CommitterToAuthor};
        const auto res = apply_strategy_sync_and_heal({input}, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].committer.timestamp == 1000);
        REQUIRE((*res)[0].committer.tz_offset_min == 60);
        REQUIRE((*res)[0].author.timestamp == 1000);
    }

    SECTION("Author = Committer") {
        Strategy5Config cfg{.sync_mode = SignatureSyncMode::AuthorToCommitter};
        const auto res = apply_strategy_sync_and_heal({input}, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author.timestamp == 2000);
        REQUIRE((*res)[0].author.tz_offset_min == 120);
        REQUIRE((*res)[0].committer.timestamp == 2000);
    }
}

TEST_CASE("Sposob 5: Konwersja stref czasowych", "[core][time_strategy][sync_heal]") {
    SECTION("PreserveInstantUtc: chwila bezwzgledna UTC nienaruszona") {
        const auto input = make_input_pair(5000, -300, 5000, -300); // UTC-5
        Strategy5Config cfg{
            .timezone_mode = TimezoneMode::PreserveInstantUtc,
            .new_tz_offset_min = 60 // UTC+1
        };

        const auto res = apply_strategy_sync_and_heal({input}, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].committer.timestamp == 5000); // UTC timestamp bez zmian
        REQUIRE((*res)[0].committer.tz_offset_min == 60);
    }

    SECTION("PreserveClockTime: godzina zegarowa zachowana w nowej strefie") {
        // 2026-11-10 14:00:00 w strefie +0100 (+60 min)
        const DateTimeCivil civil_in{.year = 2026, .month = 11, .day = 10, .hour = 14, .minute = 0, .second = 0};
        const std::int64_t ts_in = from_civil(civil_in, 60);

        const auto input = make_input_pair(ts_in, 60, ts_in, 60);
        Strategy5Config cfg{
            .timezone_mode = TimezoneMode::PreserveClockTime,
            .new_tz_offset_min = -300 // Nowa strefa: Nowy Jork (UTC-5)
        };

        const auto res = apply_strategy_sync_and_heal({input}, cfg);
        REQUIRE(res.has_value());

        // W nowej strefie czas lokalny to nadal dokładnie 14:00:00!
        const DateTimeCivil civil_out = to_civil((*res)[0].committer.timestamp, -300);
        REQUIRE(civil_out.year == 2026);
        REQUIRE(civil_out.month == 11);
        REQUIRE(civil_out.day == 10);
        REQUIRE(civil_out.hour == 14);
        REQUIRE(civil_out.minute == 0);
        REQUIRE(civil_out.second == 0);
    }
}

TEST_CASE("Sposob 5: DAG Chronology Auto-Heal", "[core][time_strategy][sync_heal]") {
    SECTION("Naprawa anomalii: dziecko starsze od rodzica") {
        const TimePoint parent_tp{.timestamp = 500, .tz_offset_min = 0};
        // Dziecko ma timestamp 200 (wcześniejszy niż rodzic 500!)
        const auto child = make_input_pair(200, 0, 200, 0, {parent_tp});

        Strategy5Config cfg{
            .auto_heal_dag_chronology = true,
            .auto_heal_step_sec = 60
        };

        const auto res = apply_strategy_sync_and_heal({child}, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].committer.timestamp == 560); // 500 + 60
        REQUIRE((*res)[0].author.timestamp == 560);
    }

    SECTION("Commit scalajacy (merge) z wieloma rodzicami") {
        const TimePoint parent1{.timestamp = 400, .tz_offset_min = 0};
        const TimePoint parent2{.timestamp = 700, .tz_offset_min = 0};
        // Merge ma timestamp 600 (późniejszy niż p1, ale wcześniejszy niż p2)
        const auto merge_commit = make_input_pair(600, 0, 600, 0, {parent1, parent2});

        Strategy5Config cfg{
            .auto_heal_dag_chronology = true,
            .auto_heal_step_sec = 10
        };

        const auto res = apply_strategy_sync_and_heal({merge_commit}, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].committer.timestamp == 710); // max(400, 700) + 10
    }

    SECTION("Commit prawidlowy (mlodszy od rodzica) pozostaje nienaruszony") {
        const TimePoint parent_tp{.timestamp = 500, .tz_offset_min = 0};
        const auto normal_commit = make_input_pair(1000, 0, 1000, 0, {parent_tp});

        Strategy5Config cfg{
            .auto_heal_dag_chronology = true,
            .auto_heal_step_sec = 60
        };

        const auto res = apply_strategy_sync_and_heal({normal_commit}, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].committer.timestamp == 1000);
    }

    SECTION("Blad przy niepoprawnym kroku auto_heal_step_sec <= 0") {
        Strategy5Config cfg{
            .auto_heal_dag_chronology = true,
            .auto_heal_step_sec = 0
        };
        REQUIRE_FALSE(apply_strategy_sync_and_heal({make_input_pair(100, 0, 100, 0)}, cfg).has_value());
    }

    SECTION("Wywolanie SyncAndHealStrategy przez interfejs ITimeStrategy synchronizuje pary po naprawie") {
        const TimePoint parent_tp{.timestamp = 1500, .tz_offset_min = 60};
        const auto commit = make_input_pair(1000, 60, 2000, 120, {parent_tp});

        const SyncAndHealStrategy strategy(Strategy5Config{
            .sync_mode = SignatureSyncMode::CommitterToAuthor,
            .auto_heal_dag_chronology = true,
            .auto_heal_step_sec = 30
        });
        const ITimeStrategy& iface = strategy;
        std::mt19937_64 rng(99);

        const auto res = iface.apply({commit}, rng, TimeTargetFlag::SyncBoth);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author.timestamp == 1530);
        REQUIRE((*res)[0].committer == (*res)[0].author);
    }
}

