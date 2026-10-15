// Loose-file content overrides from mods: <mod dir>\content\<game-relative path> replaces that game file.
// Pure logic (no Windows calls), unit tested.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

struct ContentSource {
    std::string modId;
    std::filesystem::path contentRoot;            // <mod dir>\content
    std::vector<std::filesystem::path> files;     // absolute paths below contentRoot
};

struct ContentEntry {
    std::string owner;
    std::filesystem::path file;
};

struct ContentConflict {
    std::string key;
    std::string replacedOwner;
    std::string winner;
};

class ContentRegistry {
public:
    // Sources in load order: a later source overrides an earlier one for the same game path (reported as conflict).
    void Build(const std::vector<ContentSource>& sources);

    // Normalized key of a game path as the game opens it ("DATA\\GTA.DAT", ".\\stream.ini", absolute paths inside
    // the game folder); nullopt for paths outside the game folder.
    static std::optional<std::string> NormalizeGamePath(std::string_view path, const std::filesystem::path& gameDirectory);

    const ContentEntry* Find(const std::string& key) const;
    const std::map<std::string, ContentEntry>& Entries() const { return entries_; }
    const std::vector<ContentConflict>& Conflicts() const { return conflicts_; }

private:
    std::map<std::string, ContentEntry> entries_;
    std::vector<ContentConflict> conflicts_;
};

}  // namespace sacore
