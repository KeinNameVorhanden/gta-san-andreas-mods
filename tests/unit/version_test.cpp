#include "version.h"

#include <doctest/doctest.h>

using sacore::Version;
using sacore::VersionRequirement;

namespace {
bool Satisfies(const char* requirement, const char* version) {
    const auto r = VersionRequirement::Parse(requirement);
    REQUIRE(r.has_value());
    return r->IsSatisfiedBy(*Version::Parse(version));
}
}  // namespace

TEST_CASE("version parsing rejects malformed input that a naive split would accept") {
    CHECK_FALSE(Version::Parse("1.2.3.4"));
    CHECK_FALSE(Version::Parse("1..2"));
    CHECK_FALSE(Version::Parse("1.2."));
    CHECK_FALSE(Version::Parse("v1.2"));
    CHECK_FALSE(Version::Parse("-1.0"));
    CHECK_FALSE(Version::Parse("1.2-beta"));
    CHECK(Version::Parse(" 2.10 ") == Version{2, 10, 0});
}

TEST_CASE("versions compare numerically, not lexically") {
    CHECK(*Version::Parse("1.10.0") > *Version::Parse("1.9.9"));
    CHECK(*Version::Parse("2") > *Version::Parse("1.99.99"));
}

TEST_CASE("caret requirement keeps the major version, and the minor version below 1.0") {
    CHECK(Satisfies("^1.2.3", "1.9.0"));
    CHECK_FALSE(Satisfies("^1.2.3", "2.0.0"));
    CHECK_FALSE(Satisfies("^1.2.3", "1.2.2"));
    CHECK(Satisfies("^0.3.1", "0.3.9"));
    CHECK_FALSE(Satisfies("^0.3.1", "0.4.0"));
}

TEST_CASE("tilde and combined ranges") {
    CHECK(Satisfies("~1.4", "1.4.7"));
    CHECK_FALSE(Satisfies("~1.4", "1.5.0"));
    CHECK(Satisfies(">=1.2, <2", "1.99.0"));
    CHECK_FALSE(Satisfies(">=1.2, <2", "2.0.0"));
    CHECK(Satisfies("*", "0.0.1"));
    CHECK_FALSE(VersionRequirement::Parse(">=abc"));
    CHECK_FALSE(VersionRequirement::Parse(""));
}
