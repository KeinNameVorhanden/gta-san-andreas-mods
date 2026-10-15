// Mod metadata read from <mod dir>/mod.toml.
#pragma once

#include "config.h"
#include "version.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

struct ModManifest {
    std::string id;           // [a-z0-9_.-]+, unique
    std::string name;
    Version version;
    std::string author;
    std::string description;
    int apiVersion = 0;       // major version of the core mod API the mod was built against
    std::optional<Version> minCoreVersion;
    std::vector<std::string> targetBuilds;  // empty = any supported build
    std::string dll;          // file name inside the mod directory; empty for content-only mods
    std::map<std::string, VersionRequirement> dependencies;
    std::map<std::string, VersionRequirement> optionalDependencies;
    std::vector<std::string> loadAfter;
    std::vector<std::string> loadBefore;
    std::map<std::string, bool> features;
    // [config] declarations: one table per setting with default, description, optional min/max/allowed.
    std::vector<SettingSpec> configSchema;
};

struct ManifestResult {
    std::optional<ModManifest> manifest;
    std::vector<std::string> errors;  // non-empty means the manifest is unusable
};

ManifestResult ParseModManifest(std::string_view tomlText);
ManifestResult LoadModManifest(const std::filesystem::path& file);

bool IsValidModId(std::string_view id);

}  // namespace sacore
