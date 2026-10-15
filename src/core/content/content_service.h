// Applies the content registry at runtime: the game's file opens (import kernel32!CreateFileA) for reading are
// redirected to mod files. Not supported: replacing single entries inside IMG archives (needs streaming hooks).
#pragma once

#include "content/content_registry.h"
#include "logger.h"
#include "patch_manager.h"

#include <windows.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace sacore {

class ContentService {
public:
    ContentService(Logger& logger, PatchManager& patches, std::filesystem::path gameDirectory);
    ~ContentService();
    ContentService(const ContentService&) = delete;
    ContentService& operator=(const ContentService&) = delete;

    // Builds the registry from running mods (load order) and installs the file hook if anything is overridden.
    void Apply(const std::vector<ContentSource>& sources);
    const ContentRegistry& Registry() const { return registry_; }
    size_t RedirectedOpens() const { return redirectedOpens_; }

private:
    static HANDLE WINAPI HookedCreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

    Logger& logger_;
    PatchManager& patches_;
    std::filesystem::path gameDirectory_;
    ContentRegistry registry_;
    std::set<std::string> logged_;
    size_t redirectedOpens_ = 0;
};

// Lists the files below <mod dir>\content (empty when the folder does not exist).
std::vector<std::filesystem::path> ListContentFiles(const std::filesystem::path& contentRoot);

}  // namespace sacore
