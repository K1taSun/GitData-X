#include "git/oid.hpp"

#include <array>
#include <cstring>

namespace gitdatax::git {

GitResult<Oid> Oid::from_hex(std::string_view hex) {
    if (hex.size() != kHexLength) {
        return std::unexpected(GitError{GIT_ERROR, "Oid musi miec dokladnie 40 znakow hex"});
    }

    // git_oid_fromstr wymaga napisu zakończonego zerem, a string_view go nie gwarantuje.
    const std::string terminated(hex);
    git_oid raw{};
    const int code = git_oid_fromstr(&raw, terminated.c_str());
    if (code < 0) {
        return std::unexpected(make_git_error(code));
    }
    return Oid(raw);
}

std::string Oid::to_hex() const {
    // git_oid_fmt nie dopisuje znaku końca napisu, więc długość podajemy jawnie.
    std::array<char, kHexLength> buffer{};
    git_oid_fmt(buffer.data(), &raw_);
    return std::string(buffer.data(), buffer.size());
}

bool Oid::is_zero() const {
    return git_oid_is_zero(&raw_) != 0;
}

bool operator==(const Oid& lhs, const Oid& rhs) {
    return git_oid_equal(&lhs.raw_, &rhs.raw_) != 0;
}

std::strong_ordering operator<=>(const Oid& lhs, const Oid& rhs) {
    const int cmp = git_oid_cmp(&lhs.raw_, &rhs.raw_);
    if (cmp < 0) {
        return std::strong_ordering::less;
    }
    if (cmp > 0) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

}  // namespace gitdatax::git

std::size_t std::hash<gitdatax::git::Oid>::operator()(
    const gitdatax::git::Oid& oid) const noexcept {
    // SHA jest już równomiernie "pomieszany", więc wystarczą pierwsze bajty jako hash.
    std::size_t value = 0;
    std::memcpy(&value, oid.raw().id, sizeof(value));
    return value;
}

