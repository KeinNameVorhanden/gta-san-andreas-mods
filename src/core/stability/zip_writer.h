// Minimal ZIP writer (stored entries, no compression) for crash packages. No dependency beyond the standard library,
// so it also works from the crash handler.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sacore {

struct ZipSource {
    std::string name;              // path inside the archive, forward slashes
    std::filesystem::path file;    // read from disk when `data` is empty and `file` is set
    std::vector<uint8_t> data;     // used when `file` is empty
};

uint32_t Crc32(std::span<const uint8_t> data, uint32_t crc = 0);

// Writes `zip` with every source that can be read; unreadable files are skipped and named in `skipped`.
// Returns false only when the archive itself cannot be written.
bool WriteStoredZip(const std::filesystem::path& zip, const std::vector<ZipSource>& sources,
                    std::vector<std::string>* skipped = nullptr);

}  // namespace sacore
