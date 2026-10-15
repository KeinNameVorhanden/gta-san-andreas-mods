// Typed, validated, versioned configuration stored as TOML.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sacore {

using SettingValue = std::variant<bool, int64_t, double, std::string>;

// Flattened view of a config file: "section.name" -> value.
using SettingMap = std::map<std::string, SettingValue, std::less<>>;

struct SettingSpec {
    std::string key;  // "section.name"
    SettingValue defaultValue;
    std::string description;
    std::optional<double> minimum;            // numbers only
    std::optional<double> maximum;            // numbers only
    std::vector<std::string> allowedStrings;  // strings only; empty = any
};

struct ConfigMigration {
    int fromVersion;  // migrates fromVersion -> fromVersion + 1
    std::function<void(SettingMap&)> apply;
};

struct ConfigSchema {
    int version = 1;
    std::vector<SettingSpec> settings;
    std::vector<ConfigMigration> migrations;
};

class Config {
public:
    bool GetBool(std::string_view key) const;
    int64_t GetInt(std::string_view key) const;
    double GetDouble(std::string_view key) const;
    const std::string& GetString(std::string_view key) const;
    const SettingMap& Values() const { return values_; }
    void Set(const std::string& key, SettingValue value) { values_.insert_or_assign(key, std::move(value)); }

private:
    SettingMap values_;
};

struct ConfigLoadResult {
    Config config;
    std::vector<std::string> problems;  // each problem fell back to a default and is safe to show to users
    int fileVersion = 0;                // version found in the input (0 = none)
    bool migrated = false;
    bool fileWritten = false;
    std::optional<std::filesystem::path> backup;  // copy of the user's file made before rewriting it
    SettingMap unknown;                           // keys not in the schema, preserved on rewrite
};

class ConfigLoader {
public:
    // Loads `file`, validating against `schema`:
    // - missing file: defaults, file created with descriptions as comments;
    // - unparsable file: defaults, the file is copied to "<name>.broken" before a fresh one is written;
    // - wrong type or out of range values: default for that key, reported, user's file left as is;
    // - older version: migrations applied in order, original kept as "<name>.v<old>.bak", file rewritten;
    // - newer version than the schema: values used where valid, file left untouched, reported;
    // - unknown keys: kept (also on rewrite) and reported.
    static ConfigLoadResult Load(const std::filesystem::path& file, const ConfigSchema& schema);

    // Parses and validates TOML text without file I/O.
    static ConfigLoadResult FromText(std::string_view text, const ConfigSchema& schema);

    // Parses `text` as the type of `spec` (bool: true/false/1/0) and checks its range or allowed values.
    static std::optional<SettingValue> ParseValue(const SettingSpec& spec, std::string_view text, std::string& problem);

    // Renders a full config file (with comments) for the given values.
    static std::string Render(const ConfigSchema& schema, const Config& config, const SettingMap& unknown);
};

}  // namespace sacore
