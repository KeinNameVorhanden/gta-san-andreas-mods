#include "config.h"

#include <toml++/toml.hpp>

#include <format>
#include <stdexcept>
#include <fstream>
#include <sstream>

namespace sacore {
namespace {

constexpr std::string_view kVersionKey = "config_version";

std::string TypeName(const SettingValue& value) {
    switch (value.index()) {
        case 0: return "true/false";
        case 1: return "an integer";
        case 2: return "a number";
        default: return "a string";
    }
}

std::string RenderValue(const SettingValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                // TOML basic string with the escapes the spec requires.
                std::string out = "\"";
                for (char c : v) {
                    switch (c) {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        case '\t': out += "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20) {
                                out += std::format("\\u{:04X}", static_cast<unsigned char>(c));
                            } else {
                                out += c;
                            }
                    }
                }
                return out + "\"";
            } else if constexpr (std::is_same_v<T, double>) {
                std::string text = std::format("{}", v);
                if (text.find_first_of(".eE") == std::string::npos) {
                    text += ".0";
                }
                return text;
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

void Flatten(const toml::table& table, const std::string& prefix, SettingMap& out, std::vector<std::string>& problems) {
    for (const auto& [key, node] : table) {
        const std::string name = prefix.empty() ? std::string(key.str()) : prefix + "." + std::string(key.str());
        if (const auto* sub = node.as_table()) {
            Flatten(*sub, name, out, problems);
        } else if (auto b = node.value_exact<bool>()) {
            out[name] = *b;
        } else if (auto i = node.value_exact<int64_t>()) {
            out[name] = *i;
        } else if (auto d = node.value_exact<double>()) {
            out[name] = *d;
        } else if (auto s = node.value_exact<std::string>()) {
            out[name] = *s;
        } else {
            problems.push_back(std::format("'{}' has an unsupported type and was ignored", name));
        }
    }
}

// Converts `raw` to the type of `spec.defaultValue`; integers are accepted for floating point settings.
std::optional<SettingValue> Coerce(const SettingValue& raw, const SettingSpec& spec) {
    if (raw.index() == spec.defaultValue.index()) {
        return raw;
    }
    if (std::holds_alternative<double>(spec.defaultValue) && std::holds_alternative<int64_t>(raw)) {
        return static_cast<double>(std::get<int64_t>(raw));
    }
    return std::nullopt;
}

std::optional<std::string> Validate(const SettingValue& value, const SettingSpec& spec) {
    double number = 0;
    bool isNumber = false;
    if (auto i = std::get_if<int64_t>(&value)) {
        number = static_cast<double>(*i);
        isNumber = true;
    } else if (auto d = std::get_if<double>(&value)) {
        number = *d;
        isNumber = true;
    }
    if (isNumber && ((spec.minimum && number < *spec.minimum) || (spec.maximum && number > *spec.maximum))) {
        return std::format("'{}' = {} is outside {}..{}", spec.key, RenderValue(value),
                           spec.minimum ? std::format("{}", *spec.minimum) : "",
                           spec.maximum ? std::format("{}", *spec.maximum) : "");
    }
    if (auto s = std::get_if<std::string>(&value); s && !spec.allowedStrings.empty()) {
        if (std::find(spec.allowedStrings.begin(), spec.allowedStrings.end(), *s) == spec.allowedStrings.end()) {
            std::string allowed;
            for (const auto& option : spec.allowedStrings) {
                allowed += (allowed.empty() ? "" : ", ") + option;
            }
            return std::format("'{}' = \"{}\" is not one of: {}", spec.key, *s, allowed);
        }
    }
    return std::nullopt;
}

std::optional<std::string> ReadFile(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool WriteFile(const std::filesystem::path& file, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream || !(stream << text)) {
            return false;
        }
    }
    std::filesystem::rename(temp, file, error);  // replaces atomically on the same volume
    return !error;
}

ConfigLoadResult Defaults(const ConfigSchema& schema) {
    ConfigLoadResult result;
    for (const SettingSpec& spec : schema.settings) {
        result.config.Set(spec.key, spec.defaultValue);
    }
    return result;
}

}  // namespace

bool Config::GetBool(std::string_view key) const {
    return std::get<bool>(values_.find(key)->second);
}
int64_t Config::GetInt(std::string_view key) const {
    return std::get<int64_t>(values_.find(key)->second);
}
double Config::GetDouble(std::string_view key) const {
    return std::get<double>(values_.find(key)->second);
}
const std::string& Config::GetString(std::string_view key) const {
    return std::get<std::string>(values_.find(key)->second);
}

ConfigLoadResult ConfigLoader::FromText(std::string_view text, const ConfigSchema& schema) {
    ConfigLoadResult result = Defaults(schema);
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error& error) {
        result.problems.push_back(std::format("syntax error at line {}: {}; all settings use defaults",
                                              error.source().begin.line, error.description()));
        result.fileVersion = -1;  // marks "unparsable"
        return result;
    }
    SettingMap raw;
    Flatten(table, "", raw, result.problems);

    if (auto version = raw.find(kVersionKey); version != raw.end()) {
        if (auto v = std::get_if<int64_t>(&version->second)) {
            result.fileVersion = static_cast<int>(*v);
        } else {
            result.problems.push_back("'config_version' must be an integer; treated as version 1");
            result.fileVersion = 1;
        }
        raw.erase(version);
    } else {
        result.fileVersion = 1;
    }
    if (result.fileVersion > schema.version) {
        result.problems.push_back(std::format("file has config_version {} but this build knows {}; keeping the file",
                                              result.fileVersion, schema.version));
    }
    for (int v = result.fileVersion; v < schema.version; ++v) {
        for (const ConfigMigration& migration : schema.migrations) {
            if (migration.fromVersion == v) {
                migration.apply(raw);
            }
        }
        result.migrated = true;
    }

    for (const SettingSpec& spec : schema.settings) {
        const auto found = raw.find(spec.key);
        if (found == raw.end()) {
            continue;
        }
        const auto value = Coerce(found->second, spec);
        if (!value) {
            result.problems.push_back(std::format("'{}' must be {}; using default {}", spec.key,
                                                  TypeName(spec.defaultValue), RenderValue(spec.defaultValue)));
        } else if (auto problem = Validate(*value, spec)) {
            result.problems.push_back(*problem + "; using default " + RenderValue(spec.defaultValue));
        } else {
            result.config.Set(spec.key, *value);
        }
        raw.erase(found);
    }
    for (const auto& [key, value] : raw) {
        result.problems.push_back(std::format("unknown setting '{}' (kept, not used)", key));
        result.unknown.insert_or_assign(key, value);
    }
    return result;
}

std::optional<SettingValue> ConfigLoader::ParseValue(const SettingSpec& spec, std::string_view text,
                                                     std::string& problem) {
    SettingValue value;
    const std::string s(text);
    try {
        if (std::holds_alternative<bool>(spec.defaultValue)) {
            if (s == "true" || s == "1") {
                value = true;
            } else if (s == "false" || s == "0") {
                value = false;
            } else {
                problem = std::format("'{}' must be true or false", spec.key);
                return std::nullopt;
            }
        } else if (std::holds_alternative<int64_t>(spec.defaultValue)) {
            size_t used = 0;
            value = static_cast<int64_t>(std::stoll(s, &used));
            if (used != s.size()) {
                throw std::invalid_argument("trailing characters");
            }
        } else if (std::holds_alternative<double>(spec.defaultValue)) {
            size_t used = 0;
            value = std::stod(s, &used);
            if (used != s.size()) {
                throw std::invalid_argument("trailing characters");
            }
        } else {
            value = s;
        }
    } catch (const std::exception&) {
        problem = std::format("'{}' must be {}", spec.key, TypeName(spec.defaultValue));
        return std::nullopt;
    }
    if (auto invalid = Validate(value, spec)) {
        problem = *invalid;
        return std::nullopt;
    }
    return value;
}

std::string ConfigLoader::Render(const ConfigSchema& schema, const Config& config, const SettingMap& unknown) {
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> sections;  // section -> lines
    for (const SettingSpec& spec : schema.settings) {
        const size_t dot = spec.key.rfind('.');
        const std::string section = dot == std::string::npos ? "" : spec.key.substr(0, dot);
        const std::string name = dot == std::string::npos ? spec.key : spec.key.substr(dot + 1);
        std::string comment = "# " + spec.description + " (default " + sacore::RenderValue(spec.defaultValue);
        if (spec.minimum || spec.maximum) {
            comment += std::format(", range {}..{}", spec.minimum ? std::format("{}", *spec.minimum) : "",
                                   spec.maximum ? std::format("{}", *spec.maximum) : "");
        }
        comment += ")";
        sections[section].emplace_back(comment, name + " = " + RenderValue(config.Values().at(spec.key)));
    }
    for (const auto& [key, value] : unknown) {
        const size_t dot = key.rfind('.');
        const std::string section = dot == std::string::npos ? "" : key.substr(0, dot);
        sections[section].emplace_back("# unknown setting, kept as found",
                                       (dot == std::string::npos ? key : key.substr(dot + 1)) + " = " +
                                           sacore::RenderValue(value));
    }
    std::string text = std::format("{} = {}\n", kVersionKey, schema.version);
    for (const auto& [section, lines] : sections) {
        text += section.empty() ? "\n" : std::format("\n[{}]\n", section);
        for (const auto& [comment, line] : lines) {
            text += comment + "\n" + line + "\n";
        }
    }
    return text;
}

ConfigLoadResult ConfigLoader::Load(const std::filesystem::path& file, const ConfigSchema& schema) {
    const std::optional<std::string> text = ReadFile(file);
    if (!text) {
        ConfigLoadResult result = Defaults(schema);
        result.fileWritten = WriteFile(file, Render(schema, result.config, {}));
        if (!result.fileWritten) {
            result.problems.push_back(std::format("could not create {}", file.string()));
        }
        return result;
    }
    ConfigLoadResult result = FromText(*text, schema);
    std::error_code error;
    if (result.fileVersion == -1) {
        const std::filesystem::path broken = file.string() + ".broken";
        std::filesystem::copy_file(file, broken, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            result.problems.push_back("could not back up the broken file; it was left untouched");
            return result;
        }
        result.backup = broken;
        result.fileWritten = WriteFile(file, Render(schema, result.config, result.unknown));
    } else if (result.migrated) {
        const std::filesystem::path backup = std::format("{}.v{}.bak", file.string(), result.fileVersion);
        std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            result.problems.push_back("could not back up the old file before migration; it was left untouched");
            return result;
        }
        result.backup = backup;
        result.fileWritten = WriteFile(file, Render(schema, result.config, result.unknown));
    }
    return result;
}

}  // namespace sacore
