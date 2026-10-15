#include "dependency_resolver.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using sacore::ModManifest;
using sacore::ResolveMods;
using sacore::ResolverInput;
using sacore::Version;
using sacore::VersionRequirement;

namespace {

ModManifest Mod(std::string id, Version version = {1, 0, 0}) {
    ModManifest m;
    m.id = std::move(id);
    m.name = m.id;
    m.version = version;
    m.apiVersion = 1;
    m.dll = m.id + ".dll";
    return m;
}

ModManifest& Requires(ModManifest& m, const std::string& dep, const char* requirement = "*") {
    m.dependencies.emplace(dep, *VersionRequirement::Parse(requirement));
    return m;
}

ResolverInput Input(std::vector<ModManifest> mods) {
    ResolverInput input;
    input.mods = std::move(mods);
    input.coreVersion = {0, 2, 0};
    input.apiVersion = 1;
    input.buildId = "newsteam_r2_lv";
    return input;
}

}  // namespace

TEST_CASE("dependencies load before dependents regardless of input order") {
    auto c = Mod("c");
    auto b = Mod("b");
    auto a = Mod("a");
    Requires(c, "b");
    Requires(b, "a");
    const auto result = ResolveMods(Input({c, b, a}));
    CHECK(result.rejected.empty());
    CHECK(result.loadOrder == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("a missing dependency rejects the whole chain but not unrelated mods") {
    auto top = Mod("top");
    auto mid = Mod("mid");
    Requires(top, "mid");
    Requires(mid, "missing");
    const auto result = ResolveMods(Input({top, mid, Mod("unrelated")}));
    CHECK(result.loadOrder == std::vector<std::string>{"unrelated"});
    CHECK(result.rejected.at("mid").find("not installed") != std::string::npos);
    CHECK(result.rejected.at("top").find("'mid' which failed") != std::string::npos);
}

TEST_CASE("a dependency with the wrong version is reported with both versions") {
    auto user = Mod("user");
    Requires(user, "lib", "^2.0");
    const auto result = ResolveMods(Input({user, Mod("lib", {1, 5, 0})}));
    CHECK(result.rejected.at("user").find("^2.0 but 1.5.0 is installed") != std::string::npos);
    CHECK(result.loadOrder == std::vector<std::string>{"lib"});
}

TEST_CASE("a cycle rejects its members and their dependents, others still load") {
    auto a = Mod("a");
    auto b = Mod("b");
    auto c = Mod("c");
    Requires(a, "b");
    Requires(b, "a");
    Requires(c, "a");
    const auto result = ResolveMods(Input({a, b, c, Mod("d")}));
    CHECK(result.loadOrder == std::vector<std::string>{"d"});
    CHECK(result.rejected.at("a").find("cycle") != std::string::npos);
    CHECK(result.rejected.at("b").find("cycle") != std::string::npos);
    CHECK(result.rejected.at("c").find("'a' which failed") != std::string::npos);
}

TEST_CASE("a mod only ordered after a cycle still loads") {
    auto a = Mod("a");
    auto b = Mod("b");
    auto late = Mod("late");
    a.loadAfter = {"b"};
    b.loadAfter = {"a"};
    late.loadAfter = {"a"};
    const auto result = ResolveMods(Input({a, b, late}));
    CHECK(result.rejected.contains("a"));
    CHECK(result.rejected.contains("b"));
    CHECK(result.loadOrder == std::vector<std::string>{"late"});
}

TEST_CASE("load_before and load_after order mods without making them required") {
    auto z = Mod("z");
    auto y = Mod("y");
    z.loadBefore = {"a"};
    y.loadAfter = {"absent"};
    const auto result = ResolveMods(Input({Mod("a"), z, y}));
    CHECK(result.rejected.empty());
    CHECK(result.loadOrder == std::vector<std::string>{"y", "z", "a"});
}

TEST_CASE("optional dependencies order when present and only warn when absent or mismatched") {
    auto ui = Mod("ui");
    ui.optionalDependencies.emplace("zoom", *VersionRequirement::Parse(">=1.0"));
    ui.optionalDependencies.emplace("absent", *VersionRequirement::Parse("*"));
    auto old = Mod("old");
    old.optionalDependencies.emplace("zoom", *VersionRequirement::Parse(">=5"));
    const auto result = ResolveMods(Input({ui, Mod("zoom"), old}));
    CHECK(result.rejected.empty());
    CHECK(result.loadOrder == std::vector<std::string>{"old", "zoom", "ui"});
    CHECK(result.warnings.at("ui").size() == 1);
    CHECK(result.warnings.at("old").at(0).find("not satisfied") != std::string::npos);
}

TEST_CASE("compatibility gates: duplicate ids, API version, core version, game build") {
    auto api = Mod("api");
    api.apiVersion = 2;
    auto core = Mod("core");
    core.minCoreVersion = Version{0, 3, 0};
    auto build = Mod("build");
    build.targetBuilds = {"us10_hoodlum"};
    auto fine = Mod("fine");
    fine.targetBuilds = {"us10_hoodlum", "newsteam_r2_lv"};
    const auto result = ResolveMods(Input({Mod("dup"), Mod("dup"), api, core, build, fine}));
    CHECK(result.loadOrder == std::vector<std::string>{"fine"});
    CHECK(result.rejected.at("dup").find("2 mods share") != std::string::npos);
    CHECK(result.rejected.at("api").find("API 2") != std::string::npos);
    CHECK(result.rejected.at("core").find("needs core 0.3.0") != std::string::npos);
    CHECK(result.rejected.at("build").find("newsteam_r2_lv") != std::string::npos);
}
