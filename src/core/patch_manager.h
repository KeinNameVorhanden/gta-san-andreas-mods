// Every hook and memory patch goes through here so it is registered, documented, individually
// switchable and removable.
#pragma once

#include "logger.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sacore {

struct PatchInfo {
    std::string id;        // stable id, also the config switch name ([patches] <id> = false disables it)
    std::string owner;     // "core" or a mod id
    std::string target;    // symbol or API name
    std::string reason;
    std::string expected;  // expected behaviour
    std::string onFailure; // what happens if it cannot be installed
    bool revertOnRemove = true;  // false for writes to game state variables (restoring them would corrupt state)
};

enum class PatchState { Disabled, Installed, Failed, Removed };
std::string_view ToString(PatchState state);

struct PatchRecord {
    PatchInfo info;
    PatchState state = PatchState::Failed;
    std::string detail;
    void* target = nullptr;
    bool hook = false;              // MinHook hook (removable); false = memory write
    std::vector<uint8_t> original;  // memory writes: bytes before the write
};

class PatchManager {
public:
    explicit PatchManager(Logger& logger);

    // Patch ids switched off by configuration.
    void SetDisabled(std::set<std::string> ids) { disabled_ = std::move(ids); }
    bool IsDisabled(const std::string& id) const { return disabled_.contains(id); }

    // Creates and enables a MinHook hook. Returns false (and records why) if disabled or failed.
    // A target already hooked by this manager under another id is reported as a conflict and refused.
    bool InstallHook(const PatchInfo& info, void* target, void* detour, void** original);

    // Replaces the main module's import table entry for dll!function (case-insensitive DLL name).
    // Catches calls that compatibility shims would route around an inline hook. `original` receives the old entry.
    bool InstallImportHook(const PatchInfo& info, const char* dll, const char* function, void* detour, void** original);
    // True if the main module imports dll!function by name (some builds do not import every function).
    static bool GameImports(const char* dll, const char* function);

    // Writes `size` bytes to `target` (with page protection change) and records the original bytes.
    bool WriteMemory(const PatchInfo& info, void* target, const void* bytes, size_t size);

    // Removes one installed patch (hook disabled and removed, or memory restored if revertOnRemove).
    bool Remove(const std::string& id);
    // Removes hooks and restores written memory, newest first.
    void RemoveAll();
    const PatchRecord* Find(const std::string& id) const;
    // One line per patch: id, owner, state, target, detail, reason.
    std::string StatusReport() const;

    const std::vector<PatchRecord>& Patches() const { return patches_; }

private:
    PatchRecord& Record(const PatchInfo& info);
    void Revert(PatchRecord& record);
    Logger& logger_;
    std::set<std::string> disabled_;
    std::vector<PatchRecord> patches_;
};

}  // namespace sacore
