#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include "core/time_strategies.hpp"

using namespace gitdatax::core;
using gitdatax::git::Oid;

namespace {

CommitTimeInput make_input(std::int64_t author_time, std::int64_t committer_time,
                           int tz = 60, std::vector<TimePoint> parents = {}) {
    CommitTimeInput input;
    input.dates.author = TimePoint{.timestamp = author_time, .tz_offset_min = tz};
    input.dates.committer = TimePoint{.timestamp = committer_time, .tz_offset_min = tz};
    input.parent_dates = std::move(parents);
    return input;
}

}  // namespace

TEST_CASE("Sposob 1: Data bezwzgledna", "[core][time_strategy]") {
    std::vector<CommitTimeInput> commits = {
        make_input(1000, 1050),
        make_input(2000, 2050)
    };

    const TimePoint target_time{.timestamp = 5000, .tz_offset_min = 120};

    SECTION("Modyfikacja obu sygnatur (Both)") {
        const auto res = apply_strategy_absolute(commits, target_time, DateTarget::Both);
        REQUIRE(res.has_value());
        REQUIRE(res->size() == 2);
        for (const auto& pair : *res) {
            REQUIRE(pair.author == target_time);
            REQUIRE(pair.committer == target_time);
        }
    }

    SECTION("Modyfikacja tylko Autora") {
        const auto res = apply_strategy_absolute(commits, target_time, DateTarget::AuthorOnly);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author == target_time);
        REQUIRE((*res)[0].committer.timestamp == 1050); // Nienaruszony committer
    }

    SECTION("Modyfikacja tylko Committera") {
        const auto res = apply_strategy_absolute(commits, target_time, DateTarget::CommitterOnly);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author.timestamp == 1000); // Nienaruszony author
        REQUIRE((*res)[0].committer == target_time);
    }
}

TEST_CASE("Sposob 1: Wzorce wzgledne (parent + offset)", "[core][time_strategy]") {
    const TimePoint parent_tp{.timestamp = 1000, .tz_offset_min = 60};
    std::vector<CommitTimeInput> commits = {
        make_input(0, 0, 60, {parent_tp}), // z rodzicem
        make_input(500, 500, 60, {})        // korzeń bez rodzica
    };

    const auto res = apply_strategy_parent_relative(commits, 900, DateTarget::Both); // +15m = 900s
    REQUIRE(res.has_value());
    REQUIRE((*res)[0].committer.timestamp == 1900); // 1000 + 900
    REQUIRE((*res)[1].committer.timestamp == 1400); // 500 + 900
}

TEST_CASE("Sposob 2: Przesuniecie o stala delte (Constant Delta Shift)", "[core][time_strategy]") {
    std::vector<CommitTimeInput> commits = {
        make_input(1000, 1000),
        make_input(1500, 1500),
        make_input(2200, 2200)
    };

    SECTION("Dodatnia delta zachowuje odstepy") {
        const std::int64_t delta = 86400; // +1 dzien
        const auto res = apply_strategy_delta_shift(commits, delta, DateTarget::Both);
        REQUIRE(res.has_value());

        // Niezmiennik: odległości między commitami pozostają identyczne
        REQUIRE((*res)[1].committer.timestamp - (*res)[0].committer.timestamp == 500);
        REQUIRE((*res)[2].committer.timestamp - (*res)[1].committer.timestamp == 700);

        REQUIRE((*res)[0].committer.timestamp == 1000 + 86400);
    }

    SECTION("Ujemna delta") {
        const std::int64_t delta = -300;
        const auto res = apply_strategy_delta_shift(commits, delta, DateTarget::Both);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author.timestamp == 700);
        REQUIRE((*res)[1].author.timestamp == 1200);
    }
}

TEST_CASE("Sposob 3: Interpolacja liniowa i organic jitter", "[core][time_strategy]") {
    SECTION("Przypadek N = 1") {
        std::vector<CommitTimeInput> commits = { make_input(100, 100) };
        RangeJitterConfig cfg{
            .t_start = 1000,
            .t_end = 2000,
            .seed = 42
        };

        const auto res = apply_strategy_range_jitter(commits, cfg);
        REQUIRE(res.has_value());
        REQUIRE(res->size() == 1);
        REQUIRE((*res)[0].committer.timestamp >= 1000);
        REQUIRE((*res)[0].committer.timestamp <= 2000);
    }

    SECTION("Przypadek N = 1 gdy Tstart == Tend") {
        std::vector<CommitTimeInput> commits = { make_input(100, 100) };
        RangeJitterConfig cfg{.t_start = 1000, .t_end = 1000};
        const auto res = apply_strategy_range_jitter(commits, cfg);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].committer.timestamp == 1000);
    }

    SECTION("Blad walidacji gdy przedzial za krotki na N commitow") {
        std::vector<CommitTimeInput> commits = {
            make_input(1, 1),
            make_input(2, 2),
            make_input(3, 3)
        };
        // Dla N=3 wymagane co najmniej 2 sekundy (Tstart + 2)
        RangeJitterConfig cfg{.t_start = 100, .t_end = 101};
        const auto res = apply_strategy_range_jitter(commits, cfg);
        REQUIRE_FALSE(res.has_value());
        REQUIRE_FALSE(res.error().empty());
    }

    SECTION("N > 1: gwarancja scislej monotonicznosci") {
        std::vector<CommitTimeInput> commits;
        for (int i = 0; i < 10; ++i) {
            commits.push_back(make_input(i * 10, i * 10));
        }

        RangeJitterConfig cfg{
            .t_start = 10000,
            .t_end = 20000,
            .seed = 9999
        };

        const auto res = apply_strategy_range_jitter(commits, cfg);
        REQUIRE(res.has_value());
        REQUIRE(res->size() == 10);

        REQUIRE((*res).front().committer.timestamp >= cfg.t_start);
        REQUIRE((*res).back().committer.timestamp <= cfg.t_end);

        // Ścisła monotoniczność t'_0 < t'_1 < ... < t'_{N-1}
        for (std::size_t i = 1; i < res->size(); ++i) {
            REQUIRE((*res)[i].committer.timestamp > (*res)[i - 1].committer.timestamp);
        }
    }
}

TEST_CASE("Sposob 3: RapidCheck weryfikacja monotonicznosci", "[core][time_strategy][rapidcheck]") {
    const bool passed = rc::check("scisla monotonicznosc dla dowolnych danych", []() {
        const int n = *rc::gen::inRange(2, 40);
        const auto t_start = *rc::gen::inRange<std::int64_t>(1000, 50000);
        const auto span = *rc::gen::inRange<std::int64_t>(n, 500000);
        const auto t_end = t_start + span;
        const auto seed = *rc::gen::arbitrary<std::uint64_t>();

        std::vector<CommitTimeInput> commits(n, make_input(0, 0));
        RangeJitterConfig cfg{
            .t_start = t_start,
            .t_end = t_end,
            .seed = seed
        };

        const auto res = apply_strategy_range_jitter(commits, cfg);
        RC_ASSERT(res.has_value());
        RC_ASSERT(res->size() == static_cast<std::size_t>(n));

        RC_ASSERT((*res).front().committer.timestamp >= t_start);
        RC_ASSERT((*res).back().committer.timestamp <= t_end);

        for (std::size_t i = 1; i < res->size(); ++i) {
            RC_ASSERT((*res)[i].committer.timestamp > (*res)[i - 1].committer.timestamp);
        }
    });
    REQUIRE(passed);
}

TEST_CASE("Interfejs ITimeStrategy: polimorficzne wywolania dla Trybow 1-3", "[core][time_strategy]") {
    const std::vector<CommitTimeInput> commits = {
        make_input(1000, 1000, 60, {TimePoint{.timestamp = 100, .tz_offset_min = 60}}),
        make_input(2000, 2000, 60, {TimePoint{.timestamp = 1000, .tz_offset_min = 60}}),
        make_input(3000, 3000, 60, {TimePoint{.timestamp = 2000, .tz_offset_min = 60}})
    };

    SECTION("AbsolutePresetStrategy z presetem parent + Xm i flaga ModifyAuthor") {
        const auto preset = parse_natural_preset("parent + 5m", TimePoint{.timestamp = 5000, .tz_offset_min = 60});
        REQUIRE(preset.has_value());

        const AbsolutePresetStrategy strategy(*preset);
        const ITimeStrategy& iface = strategy;
        std::mt19937_64 rng(42);

        const auto res = iface.apply(commits, rng, TimeTargetFlag::ModifyAuthor);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author.timestamp == 100 + 300);
        REQUIRE((*res)[0].committer.timestamp == 1000);
        REQUIRE((*res)[1].author.timestamp == 1000 + 300);
        REQUIRE((*res)[2].author.timestamp == 2000 + 300);
    }

    SECTION("ConstantDeltaShiftStrategy z flaga ModifyCommitter") {
        const ConstantDeltaShiftStrategy strategy(3600);
        const ITimeStrategy& iface = strategy;
        std::mt19937_64 rng(42);

        const auto res = iface.apply(commits, rng, TimeTargetFlag::ModifyCommitter);
        REQUIRE(res.has_value());
        REQUIRE((*res)[0].author.timestamp == 1000);
        REQUIRE((*res)[0].committer.timestamp == 4600);
        REQUIRE((*res)[2].committer.timestamp == 6600);
    }

    SECTION("RangeJitterStrategy deterministycznie wykorzystuje przekazany std::mt19937_64") {
        const RangeJitterStrategy strategy(RangeJitterConfig{
            .t_start = 10000,
            .t_end = 20000
        });
        const ITimeStrategy& iface = strategy;

        std::mt19937_64 rng1(777);
        std::mt19937_64 rng2(777);
        const auto res1 = iface.apply(commits, rng1, TimeTargetFlag::SyncBoth);
        const auto res2 = iface.apply(commits, rng2, TimeTargetFlag::SyncBoth);

        REQUIRE(res1.has_value());
        REQUIRE(res2.has_value());
        REQUIRE(*res1 == *res2);
        REQUIRE((*res1)[0].author == (*res1)[0].committer);
        REQUIRE((*res1)[1].committer.timestamp > (*res1)[0].committer.timestamp);
        REQUIRE((*res1)[2].committer.timestamp > (*res1)[1].committer.timestamp);
    }
}

