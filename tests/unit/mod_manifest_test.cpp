#include "mod_manifest.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using sacore::ParseModManifest;

namespace {
bool HasError(const sacore::ManifestResult& result, const std::string& fragment) {
    return std::any_of(result.errors.begin(), result.errors.end(),
                       [&](const std::string& e) { return e.find(fragment) != std::string::npos; });
}
}  // namespace

TEST_CASE("a complete manifest parses into typed fields") {
    const auto result = ParseModManifest(R"(
id = "better-hud"
name = "Better HUD"
version = "1.4.2"
api_version = 1
min_core_version = "0.2"
dll = "better_hud.dll"
target_builds = ["newsteam_r2_lv"]
load_after = ["fonts"]
[dependencies]
core-utils = "^1.0"
[optional_dependencies]
widescreen = ">=2.1"
[features]
minimap = false
)");
    REQUIRE(result.errors.empty());
    const auto& m = *result.manifest;
    CHECK(m.id == "better-hud");
    CHECK(m.version == sacore::Version{1, 4, 2});
    CHECK(m.minCoreVersion == sacore::Version{0, 2, 0});
    CHECK(m.dependencies.at("core-utils").IsSatisfiedBy({1, 9, 0}));
    CHECK_FALSE(m.dependencies.at("core-utils").IsSatisfiedBy({2, 0, 0}));
    CHECK(m.optionalDependencies.contains("widescreen"));
    CHECK(m.features.at("minimap") == false);
}

TEST_CASE("manifest errors are collected, not stopped at the first one") {
    const auto result = ParseModManifest(R"(
id = "Bad ID"
version = "one"
dll = "..\\evil.dll"
api_version = 1
[dependencies]
"Bad ID" = ">=1"
other = "not a version"
)");
    CHECK_FALSE(result.manifest);
    CHECK(HasError(result, "invalid id"));
    CHECK(HasError(result, "missing required string 'name'"));
    CHECK(HasError(result, "invalid version 'one'"));
    CHECK(HasError(result, "plain file name"));
    CHECK(HasError(result, "invalid version requirement for 'other'"));
}

TEST_CASE("config declarations become typed settings, nested tables become dotted names") {
    const auto result = ParseModManifest(R"(
id = "hud"
name = "HUD"
version = "1.0"
api_version = 1
dll = "hud.dll"
[config.enabled]
default = true
[config.hud.scale]
default = 1.5
min = 0.5
max = 3
description = "HUD scale"
[config.hud.style]
default = "classic"
allowed = ["classic", "minimal"]
[config.broken]
default = [1, 2]
)");
    CHECK(HasError(result, "'broken': default must be a bool, number or string"));

    const auto ok = ParseModManifest(R"(
id = "hud"
name = "HUD"
version = "1.0"
api_version = 1
dll = "hud.dll"
[config.enabled]
default = true
[config.hud.scale]
default = 1.5
min = 0.5
max = 3
[config.hud.style]
default = "classic"
allowed = ["classic", "minimal"]
)");
    REQUIRE(ok.manifest);
    const auto& schema = ok.manifest->configSchema;
    REQUIRE(schema.size() == 3);
    const auto scale = std::find_if(schema.begin(), schema.end(), [](const auto& s) { return s.key == "hud.scale"; });
    REQUIRE(scale != schema.end());
    CHECK(std::get<double>(scale->defaultValue) == 1.5);
    CHECK(scale->maximum == 3.0);
    const auto style = std::find_if(schema.begin(), schema.end(), [](const auto& s) { return s.key == "hud.style"; });
    CHECK(style->allowedStrings.size() == 2);
}

TEST_CASE("self dependency and syntax errors are rejected") {
    const auto self = ParseModManifest(R"(
id = "loop"
name = "Loop"
version = "1.0"
api_version = 1
dll = "loop.dll"
[dependencies]
loop = "*"
)");
    CHECK(HasError(self, "cannot depend on itself"));

    const auto syntax = ParseModManifest("id = \"x\"\nname = ");
    CHECK(HasError(syntax, "TOML syntax error at line 2"));
}
