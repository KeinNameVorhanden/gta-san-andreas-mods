#include "save_file.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <tuple>

namespace sacore {

uint32_t ComputeSaveChecksum(std::span<const uint8_t> bytes) {
    uint32_t sum = 0;
    const size_t end = std::min(bytes.size(), kSaveChecksumOffset);
    for (size_t i = 0; i < end; ++i) {
        sum += bytes[i];
    }
    return sum;
}

SaveCheck CheckSave(std::span<const uint8_t> bytes) {
    SaveCheck check;
    if (bytes.size() != kSaveFileSize) {
        check.problem = std::format("size {} bytes, expected {} (truncated or not a save)", bytes.size(), kSaveFileSize);
        return check;
    }
    if (std::memcmp(bytes.data(), "BLOCK", 5) != 0) {
        check.problem = "does not start with BLOCK";
        return check;
    }
    std::memcpy(&check.versionId, bytes.data() + 5, 4);
    std::memcpy(&check.storedChecksum, bytes.data() + kSaveChecksumOffset, 4);
    check.computedChecksum = ComputeSaveChecksum(bytes);
    if (check.storedChecksum != check.computedChecksum) {
        check.problem = std::format("checksum {:#010x} stored, {:#010x} computed: the game will reject this save",
                                    check.storedChecksum, check.computedChecksum);
        return check;
    }
    check.valid = true;
    return check;
}

namespace {
std::vector<uint8_t> ReadAll(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}  // namespace

std::optional<std::filesystem::path> BackupSaveFile(const std::filesystem::path& slotFile,
                                                    const std::filesystem::path& backupDirectory,
                                                    const std::string& stamp, size_t keep, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(slotFile, ec)) {
        return std::nullopt;
    }
    const fs::path folder = backupDirectory / slotFile.filename();
    fs::create_directories(folder, ec);
    std::vector<fs::path> existing;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.path().extension() == ".b") {
            existing.push_back(entry.path());
        }
    }
    std::sort(existing.begin(), existing.end());  // stamps sort chronologically
    const std::vector<uint8_t> current = ReadAll(slotFile);
    if (!existing.empty() && ReadAll(existing.back()) == current) {
        return std::nullopt;  // identical to the newest backup
    }
    fs::path target = folder / (stamp + ".b");
    for (int n = 2; fs::exists(target, ec); ++n) {
        target = folder / std::format("{}-{}.b", stamp, n);
    }
    fs::copy_file(slotFile, target, ec);
    if (ec) {
        error = ec.message();
        return std::nullopt;
    }
    existing.push_back(target);
    std::sort(existing.begin(), existing.end());
    while (existing.size() > keep) {
        fs::remove(existing.front(), ec);
        existing.erase(existing.begin());
    }
    return target;
}

std::optional<std::pair<std::string, int>> ParseSaveName(const std::string& name) {
    for (const auto& [prefix, stem, maximum] : {std::tuple{"slot", "GTASAsf", 8}, std::tuple{"quick", "GTASAqs", 9},
                                                std::tuple{"auto", "GTASAas", 9}, std::tuple{"story", "GTASAst", 120}}) {
        const std::string p(prefix);
        const std::string digits = name.size() > p.size() ? name.substr(p.size()) : std::string{};
        if (!name.starts_with(p) || digits.empty() || digits.size() > 3 || digits.front() == '0' ||
            digits.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        const int number = std::stoi(digits);
        if (number >= 1 && number <= maximum) {
            return std::pair{std::string(stem), number};
        }
    }
    return std::nullopt;
}

}  // namespace sacore
