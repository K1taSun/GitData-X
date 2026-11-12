#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include "core/time_strategies.hpp"
#include "core/time_utils.hpp"

using namespace gitdatax::core;

namespace {

CommitTimeInput make_commit_ts(std::int64_t ts, int tz = 60) {
    CommitTimeInput input;
    input.dates.author = TimePoint{.timestamp = ts, .tz_offset_min = tz};
    input.dates.committer = TimePoint{.timestamp = ts, .tz_offset_min = tz};
    return input;
}

}  // namespace

TEST_CASE("Sposob 6: Domykanie co do sekundy do target_full_date", "[core][time_strategy][piecewise]") {
    // Cel: 2026-11-11 16:00:00 UTC
    const std::int64_t t_target = 1794355200; // Załóżmy konkretną sekundę docelową

    SECTION("Pojedynczy commit (N = 1) trafia dokladnie w t_target") {
        PiecewiseRandomConfig cfg{
            .target_full_date = t_target,
            .delta_min_sec = 300,
            .delta_max_sec = 900,
            .seed = 42
        };

        const auto res = apply_strategy_piecewise_random_to_target({make_commit_ts(1000)}, cfg);
        REQUIRE(res.has_value());
        REQUIRE(res->size() == 1);
        REQUIRE((*res)[0].committer.timestamp == t_target);
    }

    SECTION("Wielu commitow w jednym segmencie domyka sie w t_target") {
        std::vector<CommitTimeInput> commits;
        for (int i = 0; i < 15; ++i) {
            commits.push_back(make_commit_ts(i * 100));
        }

        PiecewiseRandomConfig cfg{
            .target_full_date = t_target,
            .delta_min_sec = 300,
            .delta_max_sec = 1200,
            .seed = 1337
        };

        const auto res = apply_strategy_piecewise_random_to_target(commits, cfg);
        REQUIRE(res.has_value());
        REQUIRE(res->size() == 15);

        // Ostatni commit MUSI trafić co do sekundy w t_target!
        REQUIRE((*res).back().committer.timestamp == t_target);

        // Wszelkie commity są ściśle rosnące
        for (std::size_t i = 1; i < res->size(); ++i) {
            REQUIRE((*res)[i].committer.timestamp > (*res)[i - 1].committer.timestamp);
        }
    }
}

TEST_CASE("Sposob 6: Podzial na odcinki sesyjne", "[core][time_strategy][piecewise]") {
    const std::int64_t t_target = 200000;

    // 2 commity z wczoraj, przerwa 10 godzin (36000s > auto_session_gap 6h), 3 commity z dzisiaj
    std::vector<CommitTimeInput> commits = {
        make_commit_ts(1000),
        make_commit_ts(1500),
        make_commit_ts(1500 + 36000), // Automatyczne cięcie na nowy odcinek
        make_commit_ts(1500 + 36500),
        make_commit_ts(1500 + 37000)
    };

    PiecewiseRandomConfig cfg{
        .target_full_date = t_target,
        .delta_min_sec = 300,
        .delta_max_sec = 900,
        .auto_session_gap_sec = 6 * 3600,
        .seed = 777
    };

    const auto res = apply_strategy_piecewise_random_to_target(commits, cfg);
    REQUIRE(res.has_value());
    REQUIRE(res->size() == 5);

    // Całość jest ściśle monotoniczna
    for (std::size_t i = 1; i < res->size(); ++i) {
        REQUIRE((*res)[i].committer.timestamp > (*res)[i - 1].committer.timestamp);
    }

    // Finalny commit ma dokładnie t_target
    REQUIRE((*res).back().committer.timestamp == t_target);
}

TEST_CASE("Sposob 6: Przelosowanie ze zmiennym seedem", "[core][time_strategy][piecewise]") {
    const std::int64_t t_target = 500000;
    std::vector<CommitTimeInput> commits;
    for (int i = 0; i < 8; ++i) {
        commits.push_back(make_commit_ts(i * 100));
    }

    PiecewiseRandomConfig cfg1{
        .target_full_date = t_target,
        .delta_min_sec = 300,
        .delta_max_sec = 900,
        .seed = 111
    };

    PiecewiseRandomConfig cfg2 = cfg1;
    cfg2.seed = 222;

    const auto res1 = apply_strategy_piecewise_random_to_target(commits, cfg1);
    const auto res2 = apply_strategy_piecewise_random_to_target(commits, cfg2);

    REQUIRE(res1.has_value());
    REQUIRE(res2.has_value());

    // Oba domykają się dokładnie w t_target
    REQUIRE((*res1).back().committer.timestamp == t_target);
    REQUIRE((*res2).back().committer.timestamp == t_target);

    // Ale pośrednie commity mają różne czasy z uwagi na inny seed
    bool any_difference = false;
    for (std::size_t i = 0; i < res1->size() - 1; ++i) {
        if ((*res1)[i].committer.timestamp != (*res2)[i].committer.timestamp) {
            any_difference = true;
            break;
        }
    }
    REQUIRE(any_difference);
}

TEST_CASE("Sposob 6: RapidCheck weryfikacja domykania i monotonicznosci", "[core][time_strategy][piecewise][rapidcheck]") {
    const bool passed = rc::check("ostatni commit rowny t_target oraz scisla monotonicznosc", []() {
        const int n = *rc::gen::inRange(2, 25);
        const auto target_time = *rc::gen::inRange<std::int64_t>(1000000, 20000000);
        const auto seed = *rc::gen::arbitrary<std::uint64_t>();

        std::vector<CommitTimeInput> commits(n, make_commit_ts(0));
        PiecewiseRandomConfig cfg{
            .target_full_date = target_time,
            .delta_min_sec = 100,
            .delta_max_sec = 500,
            .seed = seed
        };

        const auto res = apply_strategy_piecewise_random_to_target(commits, cfg);
        RC_ASSERT(res.has_value());
        RC_ASSERT(res->size() == static_cast<std::size_t>(n));

        // Niezmiennik domknięcia
        RC_ASSERT((*res).back().committer.timestamp == target_time);

        // Niezmiennik monotoniczności
        for (std::size_t i = 1; i < res->size(); ++i) {
            RC_ASSERT((*res)[i].committer.timestamp > (*res)[i - 1].committer.timestamp);
        }
    });
    REQUIRE(passed);
}

TEST_CASE("Sposob 6: Podzial dzienny split_by_day, walidacja odcinkow oraz ITimeStrategy", "[core][time_strategy][piecewise]") {
    const auto day1_a = parse_iso8601("2026-11-10 10:00:00 +0100");
    const auto day1_b = parse_iso8601("2026-11-10 10:20:00 +0100");
    // Różnica tylko 2h (poniżej auto_session_gap_sec = 6h), ale przekracza północ -> nowy dzień!
    const auto day2_a = parse_iso8601("2026-11-11 00:15:00 +0100");
    const auto day2_b = parse_iso8601("2026-11-11 00:40:00 +0100");
    const auto target_tp = parse_iso8601("2026-11-12 18:00:00 +0100");
    REQUIRE(day1_a.has_value());
    REQUIRE(day1_b.has_value());
    REQUIRE(day2_a.has_value());
    REQUIRE(day2_b.has_value());
    REQUIRE(target_tp.has_value());

    const std::vector<CommitTimeInput> commits = {
        make_commit_ts(day1_a->timestamp, 60),
        make_commit_ts(day1_b->timestamp, 60),
        make_commit_ts(day2_a->timestamp, 60),
        make_commit_ts(day2_b->timestamp, 60)
    };

    const PiecewiseRandomStrategy strategy(PiecewiseRandomConfig{
        .target_full_date = target_tp->timestamp,
        .delta_min_sec = 300,
        .delta_max_sec = 900,
        .auto_session_gap_sec = 6 * 3600,
        .split_by_day = true
    });
    const ITimeStrategy& iface = strategy;
    std::mt19937_64 rng(555);

    const auto res = iface.apply(commits, rng, TimeTargetFlag::SyncBoth);
    REQUIRE(res.has_value());
    REQUIRE(res->size() == 4);
    REQUIRE(res->back().committer.timestamp == target_tp->timestamp);
    // Między dniem 1 (indeks 1) a dniem 2 (indeks 2) musi powstać wyraźna przerwa międzysesyjna (>= 6h)
    REQUIRE((*res)[2].committer.timestamp - (*res)[1].committer.timestamp >= 6 * 3600);

    // Walidacja wyjścia poza zakres w explicit_segments
    PiecewiseRandomConfig bad_seg{
        .explicit_segments = {
            TimeSegment{.start_index = 2, .count = 5, .t_start = 1000, .t_target = 5000}
        }
    };
    REQUIRE_FALSE(apply_strategy_piecewise_random_to_target(commits, bad_seg).has_value());
}

