#include "content_registry.h"

#include <algorithm>
#include <cctype>

namespace sacore {
namespace {

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Lowercase, backslashes, no "." segments, no leading separators; empty if the path climbs out with "..".
std::string Canonical(std::string_view path) {
    std::vector<std::string> parts;
    std::string part;
    auto flush = [&]() -> bool {
        if (part.empty() || part == ".") {
        } else if (part == "..") {
            if (parts.empty()) {
                return false;
            }
            parts.pop_back();
        } else {
            parts.push_back(Lower(part));
        }
        part.clear();
        return true;
    };
    for (char c : path) {
        if (c == '\\' || c == '/') {
            if (!flush()) {
                return {};
            }
        } else {
            part += c;
        }
    }
    if (!flush()) {
        return {};
    }
    std::string out;
    for (const std::string& p : parts) {
        out += (out.empty() ? "" : "\\") + p;
    }
    return out;
}

}  // namespace

std::optional<std::string> ContentRegistry::NormalizeGamePath(std::string_view path,
                                                              const std::filesystem::path& gameDirectory) {
    const bool absolute = (path.size() >= 2 && path[1] == ':') || path.starts_with("\\\\");
    std::string relative(path);
    if (absolute) {
        const std::string game = Canonical(gameDirectory.string());
        const std::string full = Canonical(path);
        if (game.empty() || full.size() <= game.size() || full.compare(0, game.size(), game) != 0 ||
            full[game.size()] != '\\') {
            return std::nullopt;
        }
        relative = full.substr(game.size() + 1);
    }
    const std::string key = Canonical(relative);
    if (key.empty()) {
        return std::nullopt;
    }
    return key;
}

void ContentRegistry::Build(const std::vector<ContentSource>& sources) {
    entries_.clear();
    conflicts_.clear();
    for (const ContentSource& source : sources) {
        for (const std::filesystem::path& file : source.files) {
            const std::string key = Canonical(file.lexically_relative(source.contentRoot).string());
            if (key.empty()) {
                continue;
            }
            auto [it, inserted] = entries_.try_emplace(key, ContentEntry{source.modId, file});
            if (!inserted) {
                conflicts_.push_back({key, it->second.owner, source.modId});
                it->second = ContentEntry{source.modId, file};
            }
        }
    }
}

const ContentEntry* ContentRegistry::Find(const std::string& key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

}  // namespace sacore
