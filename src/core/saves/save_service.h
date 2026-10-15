// Save-file features: per-install User Files folder, automatic backups, validation, slot tracking,
// per-slot side-car data for mods, and a save call for the test harness.
#pragma once

#include "event_bus.h"
#include "game/adapter.h"
#include "logger.h"
#include "patch_manager.h"

#include <windows.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sacore {

struct SaveSettings {
    bool separateUserFiles = false;  // keep this install's saves and gta_sa.set in <game>\sacore\userdata
    std::string redirectBase;        // active redirect from the early hook (empty = shared folder)
    std::string redirectProblem;     // why the redirect is inactive although enabled
    int backupsPerSlot = 10;
};

class SaveService {
public:
    SaveService(Logger& logger, PatchManager& patches, EventBus& bus, const GameAdapter& adapter,
                std::filesystem::path dataDirectory);
    ~SaveService();
    SaveService(const SaveService&) = delete;
    SaveService& operator=(const SaveService&) = delete;

    void Install(const SaveSettings& settings);

    std::filesystem::path UserFilesDirectory() const;
    std::filesystem::path SlotFile(int slot) const;  // slot 1..8
    int CurrentSlot() const { return currentSlot_; }  // 1..8 from the last save or load, 0 if unknown
    // Saves into `slot` (1..8) through the game's own routine; game thread only.
    bool SaveToSlot(int slot);
    // Named saves: "slot1".."slot8" (the menu slots), "quick1".."quick9" and "auto1".."auto9" (own files that never
    // touch the menu slots). Game thread only, from the message loop.
    bool SaveTo(const std::string& name);
    std::filesystem::path FileFor(const std::string& name) const;  // empty path for an invalid name
    std::string CurrentSave() const { return currentSave_; }     // name of the last save or load, "" if none
    // Every existing save: name, file, last write time (UTC, seconds), valid.
    struct SaveEntry {
        std::string name;
        std::filesystem::path file;
        long long modified = 0;
        bool valid = false;
    };
    std::vector<SaveEntry> ListSaves() const;
    // Save files dropped into sacore\import (community saves): name, size, check result.
    struct ImportEntry {
        std::string file;  // file name inside the import folder
        uintmax_t size = 0;
        bool valid = false;
        std::string problem;
        uint32_t versionId = 0;
    };
    std::filesystem::path ImportDirectory() const { return dataDirectory_ / L"import"; }
    std::vector<ImportEntry> ListImports() const;
    // Copies a valid file from the import folder over the save `name` ("slot3", "quick1", ...); the save it replaces is
    // backed up first. Returns "" on success, else the reason.
    std::string Import(const std::string& file, const std::string& name) const;
    // Loads a save the way the menu does (file name set, restart with load). From the main menu (game state 7) or
    // while playing (9); game thread, message loop. False (logged) when the file is missing or damaged.
    bool LoadFrom(const std::string& name);
    // Validates every slot file and logs the result; returns the number of invalid slots.
    int ValidateAll() const;

    // Side-car data owned by mods, stored next to the save of the current slot.
    void SetModValue(const std::string& mod, const std::string& key, const std::string& value);
    std::optional<std::string> GetModValue(const std::string& mod, const std::string& key) const;

private:
    static void __cdecl HookedMakeValidSaveName(int slotIndex);
    static char* __cdecl HookedGetUserFilesFolder();
    void BackupSlot(int slot, const char* reason) const;
    void BackupFile(const std::filesystem::path& file, const std::string& name, const char* reason) const;
    void WriteSideCar(const std::string& name) const;
    void ReadSideCar(const std::string& name);
    std::filesystem::path SideCarDirectory(const std::string& name) const;

    Logger& logger_;
    PatchManager& patches_;
    EventBus& bus_;
    const GameAdapter& adapter_;
    std::filesystem::path dataDirectory_;
    SaveSettings settings_;
    std::string userFilesFolder_;  // redirect target including "\\GTA San Andreas User Files"
    std::string redirectBase_;  // ANSI path handed to the game instead of "My Documents"
    int currentSlot_ = 0;
    std::string currentSave_;      // "slot3", "quick1", ...
    std::string pendingName_;      // next MakeValidSaveName call is redirected to this save
    std::map<std::string, std::map<std::string, std::string>> sideCar_;  // mod -> key -> value
};

}  // namespace sacore
