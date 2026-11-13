#include <catch2/catch_test_macros.hpp>
#include <git2.h>
#include <git2/sys/errors.h>

#include "git/error.hpp"

using gitdatax::git::check_git_call;
using gitdatax::git::GitLibInit;
using gitdatax::git::GitResult;
using gitdatax::git::make_git_error;

TEST_CASE("GitLibInit mozna tworzyc zagniezdzenie", "[git][error]") {
    GitLibInit outer;
    {
        GitLibInit inner;
        SUCCEED();
    }
    // Po zniszczeniu inner biblioteka dalej działa dzięki licznikowi referencji.
    git_oid oid;
    REQUIRE(git_oid_fromstr(&oid, "0123456789abcdef0123456789abcdef01234567") == 0);
}

TEST_CASE("check_git_call zwraca sukces dla kodu >= 0", "[git][error]") {
    REQUIRE(check_git_call(0).has_value());
    REQUIRE(check_git_call(5).has_value());
}

TEST_CASE("check_git_call zwraca blad z kodem i komunikatem", "[git][error]") {
    GitLibInit init;
    git_oid oid;
    const int code = git_oid_fromstr(&oid, "to-nie-jest-hex");
    REQUIRE(code < 0);

    const GitResult<void> result = check_git_call(code);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == code);
    REQUIRE_FALSE(result.error().message.empty());
}

TEST_CASE("make_git_error ma komunikat zastepczy bez bledu libgit2", "[git][error]") {
    GitLibInit init;
    git_error_clear();
    const auto error = make_git_error(-1);
    REQUIRE(error.code == -1);
    REQUIRE_FALSE(error.message.empty());
}

