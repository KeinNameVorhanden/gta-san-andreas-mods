// GTA SA save file checks and backup rotation (pure logic, unit tested).
// Format reference: gtamods "Saves (GTA SA)"; verified on real saves.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sacore {

inline constexpr size_t kSaveFileSize = 0x31800;
inline constexpr size_t kSaveChecksumOffset = 0x317FC;  // checksum covers the bytes before it

struct SaveCheck {
    bool valid = false;
    std::string problem;          // empty when valid
    uint32_t storedChecksum = 0;
    uint32_t computedChecksum = 0;
    uint32_t versionId = 0;       // first dword of block 0 (after "BLOCK")
};

SaveCheck CheckSave(std::span<const uint8_t> bytes);
// Writes the checksum a save must carry (used by tests and by repair tooling).
uint32_t ComputeSaveChecksum(std::span<const uint8_t> bytes);

// Copies `slotFile` to <backupDirectory>/<slot file name>/<stamp>.b unless it is identical to the newest backup,
// then deletes the oldest backups beyond `keep`. Returns the new backup path, or nullopt when nothing was copied.
std::optional<std::filesystem::path> BackupSaveFile(const std::filesystem::path& slotFile,
                                                    const std::filesystem::path& backupDirectory,
                                                    const std::string& stamp, size_t keep, std::string& error);

// "slot3" -> ("GTASAsf", 3), "quick2" -> ("GTASAqs", 2), "auto1" -> ("GTASAas", 1), "story57" -> ("GTASAst", 57);
// nullopt when invalid. Story saves are the story runner's library of story points (one per mission).
std::optional<std::pair<std::string, int>> ParseSaveName(const std::string& name);

}  // namespace sacore
