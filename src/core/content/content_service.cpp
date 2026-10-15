#include "content_service.h"

#include <algorithm>
#include <format>

namespace sacore {
namespace {

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

ContentService* g_content = nullptr;
CreateFileAFn g_origCreateFileA = nullptr;

}  // namespace

std::vector<std::filesystem::path> ListContentFiles(const std::filesystem::path& contentRoot) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(contentRoot, error)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRoot, error)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

ContentService::ContentService(Logger& logger, PatchManager& patches, std::filesystem::path gameDirectory)
    : logger_(logger), patches_(patches), gameDirectory_(std::move(gameDirectory)) {
    g_content = this;
}

ContentService::~ContentService() {
    g_content = nullptr;
}

void ContentService::Apply(const std::vector<ContentSource>& sources) {
    registry_.Build(sources);
    for (const ContentConflict& conflict : registry_.Conflicts()) {
        logger_.Logf(LogLevel::Warn, LogCategory::Resources, "content", "{}: {} overrides {} (load order)", conflict.key,
                     conflict.winner, conflict.replacedOwner);
    }
    if (registry_.Entries().empty()) {
        logger_.Log(LogLevel::Info, LogCategory::Resources, "content", "no content overrides");
        return;
    }
    logger_.Logf(LogLevel::Info, LogCategory::Resources, "content", "{} file override(s) from {} mod(s)",
                 registry_.Entries().size(), sources.size());
    patches_.InstallImportHook({"content.file_overrides", "core", "kernel32!CreateFileA (game imports)",
                                "let mods replace game files without touching the install",
                                "game file opens for reading use the mod's file when one is registered",
                                "mod content is ignored; original game files are used"},
                               "KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(&HookedCreateFileA),
                               reinterpret_cast<void**>(&g_origCreateFileA));
}

HANDLE WINAPI ContentService::HookedCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                               DWORD disposition, DWORD flags, HANDLE templateFile) {
    // Only reads of existing files are redirected; writes (saves, settings, logs) always go to the original path.
    if (g_content && name && (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) == 0 &&
        disposition == OPEN_EXISTING) {
        if (const auto key = ContentRegistry::NormalizeGamePath(name, g_content->gameDirectory_)) {
            if (const ContentEntry* entry = g_content->registry_.Find(*key)) {
                ++g_content->redirectedOpens_;
                if (g_content->logged_.insert(*key).second) {
                    g_content->logger_.Logf(LogLevel::Info, LogCategory::Resources, "content", "{} -> {} ({})", *key,
                                            entry->file.string(), entry->owner);
                }
                return CreateFileW(entry->file.c_str(), access, share, security, disposition, flags, templateFile);
            }
        }
    }
    return g_origCreateFileA(name, access, share, security, disposition, flags, templateFile);
}

}  // namespace sacore
