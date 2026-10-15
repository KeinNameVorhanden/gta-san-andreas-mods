#include "config.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using sacore::ConfigLoader;
using sacore::ConfigSchema;
using sacore::SettingMap;
using sacore::SettingSpec;

namespace {

ConfigSchema WindowSchema() {
    ConfigSchema schema;
    schema.version = 1;
    schema.settings = {
        SettingSpec{"window.enabled", true, "Run the game in a window", {}, {}, {}},
        SettingSpec{"window.width", int64_t{1280}, "Client width in pixels", 320.0, 7680.0, {}},
        SettingSpec{"window.scale", 1.0, "Render scale", 0.25, 4.0, {}},
        SettingSpec{"log.level", std::string("info"), "Minimum log level", {}, {}, {"trace", "debug", "info"}},
    };
    return schema;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() / ("sacore_test_" + std::to_string(random()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::string Slurp(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void Spit(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

TEST_CASE("invalid values fall back per key and keep the valid ones") {
    const auto result = ConfigLoader::FromText(R"(
[window]
enabled = "yes"
width = 99999
scale = 2
[log]
level = "verbose"
)",
                                               WindowSchema());
    CHECK(result.config.GetBool("window.enabled") == true);    // wrong type -> default
    CHECK(result.config.GetInt("window.width") == 1280);       // out of range -> default
    CHECK(result.config.GetDouble("window.scale") == 2.0);     // integer accepted for a float setting
    CHECK(result.config.GetString("log.level") == "info");     // not an allowed value -> default
    CHECK(result.problems.size() == 3);
}

TEST_CASE("missing file is created with defaults and descriptions, and loads back identically") {
    TempDir dir;
    const auto file = dir.path / "core.toml";
    const auto first = ConfigLoader::Load(file, WindowSchema());
    CHECK(first.fileWritten);
    CHECK(first.problems.empty());
    const std::string text = Slurp(file);
    CHECK(text.find("# Client width in pixels (default 1280, range 320..7680)") != std::string::npos);
    const auto second = ConfigLoader::Load(file, WindowSchema());
    CHECK_FALSE(second.fileWritten);
    CHECK(second.problems.empty());
    CHECK(second.config.Values() == first.config.Values());
}

TEST_CASE("a broken file is preserved as .broken before defaults are written") {
    TempDir dir;
    const auto file = dir.path / "core.toml";
    Spit(file, "[window\nwidth = 800\n");
    const auto result = ConfigLoader::Load(file, WindowSchema());
    REQUIRE(result.backup);
    CHECK(Slurp(*result.backup) == "[window\nwidth = 800\n");
    CHECK(result.config.GetInt("window.width") == 1280);
    CHECK(ConfigLoader::Load(file, WindowSchema()).problems.empty());
}

TEST_CASE("migration renames a key, keeps the user's value, backs up the old file and keeps unknown keys") {
    TempDir dir;
    const auto file = dir.path / "core.toml";
    const std::string old = "config_version = 1\n[window]\nsize = 1600\n[custom]\nnote = \"mine\"\n";
    Spit(file, old);
    ConfigSchema schema = WindowSchema();
    schema.version = 2;
    schema.migrations.push_back({1, [](SettingMap& values) {
                                     if (auto node = values.extract("window.size")) {
                                         node.key() = "window.width";
                                         values.insert(std::move(node));
                                     }
                                 }});
    const auto result = ConfigLoader::Load(file, schema);
    CHECK(result.migrated);
    CHECK(result.config.GetInt("window.width") == 1600);
    REQUIRE(result.backup);
    CHECK(Slurp(*result.backup) == old);
    const std::string rewritten = Slurp(file);
    CHECK(rewritten.find("config_version = 2") != std::string::npos);
    CHECK(rewritten.find("note = \"mine\"") != std::string::npos);
    const auto again = ConfigLoader::Load(file, schema);
    CHECK_FALSE(again.migrated);
    CHECK(again.config.GetInt("window.width") == 1600);
}

TEST_CASE("a file from a newer version is used but never rewritten") {
    TempDir dir;
    const auto file = dir.path / "core.toml";
    const std::string newer = "config_version = 9\n[window]\nwidth = 1920\n";
    Spit(file, newer);
    const auto result = ConfigLoader::Load(file, WindowSchema());
    CHECK(result.config.GetInt("window.width") == 1920);
    CHECK_FALSE(result.fileWritten);
    CHECK(Slurp(file) == newer);
    CHECK(result.problems.size() == 1);
}

TEST_CASE("live setting text is parsed as the declared type and checked against its range") {
    const SettingSpec width{"window.client_width", int64_t{0}, "", 0.0, 7680.0, {}};
    const SettingSpec scale{"radar.zoom", 1.0, "", 0.5, 4.0, {}};
    const SettingSpec muted{"audio.start_muted", false, "", {}, {}, {}};
    const SettingSpec view{"devtools.view", std::string("advanced"), "", {}, {}, {"beginner", "advanced", "expert"}};
    std::string problem;
    CHECK(std::get<int64_t>(*ConfigLoader::ParseValue(width, "1920", problem)) == 1920);
    CHECK_FALSE(ConfigLoader::ParseValue(width, "8000", problem));  // above the maximum
    CHECK(problem.find("outside") != std::string::npos);
    CHECK_FALSE(ConfigLoader::ParseValue(width, "12px", problem));  // trailing text is not a number
    CHECK_FALSE(ConfigLoader::ParseValue(width, "", problem));
    CHECK(std::get<double>(*ConfigLoader::ParseValue(scale, "2", problem)) == doctest::Approx(2.0));
    CHECK_FALSE(ConfigLoader::ParseValue(scale, "0.1", problem));
    CHECK(std::get<bool>(*ConfigLoader::ParseValue(muted, "1", problem)));
    CHECK_FALSE(ConfigLoader::ParseValue(muted, "yes", problem));
    CHECK(std::get<std::string>(*ConfigLoader::ParseValue(view, "expert", problem)) == "expert");
    CHECK_FALSE(ConfigLoader::ParseValue(view, "god", problem));
}
