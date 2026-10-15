#include "mod_manifest.h"

#include <toml++/toml.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace sacore {
namespace {

void ReadStringList(const toml::table& table, std::string_view key, std::vector<std::string>& out,
                    std::vector<std::string>& errors) {
    const toml::node* node = table.get(key);
    if (!node) {
        return;
    }
    const toml::array* array = node->as_array();
    if (!array) {
        errors.push_back(std::format("'{}' must be an array of strings", key));
        return;
    }
    for (const toml::node& element : *array) {
        if (const auto* text = element.as_string()) {
            out.push_back(text->get());
        } else {
            errors.push_back(std::format("'{}' must contain only strings", key));
        }
    }
}

void ReadRequirements(const toml::table& table, std::string_view key,
                      std::map<std::string, VersionRequirement>& out, std::vector<std::string>& errors) {
    const toml::node* node = table.get(key);
    if (!node) {
        return;
    }
    const toml::table* deps = node->as_table();
    if (!deps) {
        errors.push_back(std::format("'[{}]' must be a table of id = \"requirement\"", key));
        return;
    }
    for (const auto& [depKey, depValue] : *deps) {
        const std::string depId(depKey.str());
        const auto* text = depValue.as_string();
        const auto requirement = text ? VersionRequirement::Parse(text->get()) : std::nullopt;
        if (!IsValidModId(depId)) {
            errors.push_back(std::format("[{}]: invalid mod id '{}'", key, depId));
        } else if (!requirement) {
            errors.push_back(std::format("[{}]: invalid version requirement for '{}'", key, depId));
        } else {
            out.emplace(depId, *requirement);
        }
    }
}

// Reads [config] setting declarations. A table with a "default" key is a setting; other tables nest names
// ("hud.scale" comes from [config.hud.scale]).
void ReadConfigSchema(const toml::table& table, const std::string& prefix, std::vector<SettingSpec>& out,
                      std::vector<std::string>& errors) {
    for (const auto& [key, node] : table) {
        const std::string name = prefix.empty() ? std::string(key.str()) : prefix + "." + std::string(key.str());
        const toml::table* setting = node.as_table();
        if (!setting) {
            errors.push_back(std::format("[config] '{}' must be a table with at least 'default'", name));
            continue;
        }
        const toml::node* def = setting->get("default");
        if (!def) {
            ReadConfigSchema(*setting, name, out, errors);
            continue;
        }
        SettingSpec spec;
        spec.key = name;
        spec.description = (*setting)["description"].value_or(std::string{});
        if (auto b = def->value_exact<bool>()) {
            spec.defaultValue = *b;
        } else if (auto i = def->value_exact<int64_t>()) {
            spec.defaultValue = *i;
        } else if (auto d = def->value_exact<double>()) {
            spec.defaultValue = *d;
        } else if (auto s = def->value_exact<std::string>()) {
            spec.defaultValue = *s;
        } else {
            errors.push_back(std::format("[config] '{}': default must be a bool, number or string", name));
            continue;
        }
        if (auto min = (*setting)["min"].value<double>()) {
            spec.minimum = *min;
        }
        if (auto max = (*setting)["max"].value<double>()) {
            spec.maximum = *max;
        }
        if (const toml::array* allowed = (*setting)["allowed"].as_array()) {
            for (const toml::node& option : *allowed) {
                if (auto text = option.value<std::string>()) {
                    spec.allowedStrings.push_back(*text);
                }
            }
        }
        out.push_back(std::move(spec));
    }
}

}  // namespace

bool IsValidModId(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

ManifestResult ParseModManifest(std::string_view tomlText) {
    ManifestResult result;
    toml::table table;
    try {
        table = toml::parse(tomlText);
    } catch (const toml::parse_error& error) {
        result.errors.push_back(std::format("TOML syntax error at line {}: {}", error.source().begin.line,
                                            error.description()));
        return result;
    }

    ModManifest m;
    auto required = [&](std::string_view key) -> std::optional<std::string> {
        if (auto value = table[key].value<std::string>()) {
            return value;
        }
        result.errors.push_back(std::format("missing required string '{}'", key));
        return std::nullopt;
    };
    if (auto id = required("id")) {
        m.id = *id;
        if (!IsValidModId(m.id)) {
            result.errors.push_back(std::format("invalid id '{}': use 1 to 64 of [a-z0-9_.-]", m.id));
        }
    }
    if (auto name = required("name")) {
        m.name = *name;
    }
    if (auto version = required("version")) {
        if (auto parsed = Version::Parse(*version)) {
            m.version = *parsed;
        } else {
            result.errors.push_back(std::format("invalid version '{}'", *version));
        }
    }
    // "dll" is optional: a mod without code can still provide content (<mod dir>\content).
    if (auto dll = table["dll"].value<std::string>()) {
        m.dll = *dll;
        if (m.dll.empty() || m.dll.find_first_of("/\\:") != std::string::npos) {
            result.errors.push_back("'dll' must be a plain file name inside the mod directory");
        }
    }
    if (auto api = table["api_version"].value<int64_t>()) {
        m.apiVersion = static_cast<int>(*api);
    } else {
        result.errors.push_back("missing required integer 'api_version'");
    }
    m.author = table["author"].value_or(std::string{});
    m.description = table["description"].value_or(std::string{});
    if (auto minCore = table["min_core_version"].value<std::string>()) {
        m.minCoreVersion = Version::Parse(*minCore);
        if (!m.minCoreVersion) {
            result.errors.push_back(std::format("invalid min_core_version '{}'", *minCore));
        }
    }
    ReadStringList(table, "target_builds", m.targetBuilds, result.errors);
    ReadStringList(table, "load_after", m.loadAfter, result.errors);
    ReadStringList(table, "load_before", m.loadBefore, result.errors);
    ReadRequirements(table, "dependencies", m.dependencies, result.errors);
    ReadRequirements(table, "optional_dependencies", m.optionalDependencies, result.errors);
    if (const toml::table* features = table["features"].as_table()) {
        for (const auto& [key, value] : *features) {
            if (auto flag = value.value<bool>()) {
                m.features.emplace(std::string(key.str()), *flag);
            } else {
                result.errors.push_back(std::format("[features]: '{}' must be true or false", key.str()));
            }
        }
    }
    if (const toml::table* config = table["config"].as_table()) {
        ReadConfigSchema(*config, "", m.configSchema, result.errors);
    }
    if (m.dependencies.contains(m.id) || m.optionalDependencies.contains(m.id)) {
        result.errors.push_back("a mod cannot depend on itself");
    }
    if (result.errors.empty()) {
        result.manifest = std::move(m);
    }
    return result;
}

ManifestResult LoadModManifest(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        ManifestResult result;
        result.errors.push_back(std::format("cannot open {}", file.string()));
        return result;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return ParseModManifest(buffer.str());
}

}  // namespace sacore
