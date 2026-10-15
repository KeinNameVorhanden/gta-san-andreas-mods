#include "saves/save_file.h"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using sacore::BackupSaveFile;
using sacore::CheckSave;
using sacore::ComputeSaveChecksum;
using sacore::kSaveChecksumOffset;
using sacore::kSaveFileSize;
using sacore::ParseSaveName;

namespace {

std::vector<uint8_t> MakeSave(uint32_t versionId, uint8_t fill) {
    std::vector<uint8_t> save(kSaveFileSize, fill);
    std::memcpy(save.data(), "BLOCK", 5);
    std::memcpy(save.data() + 5, &versionId, 4);
    const uint32_t sum = ComputeSaveChecksum(save);
    std::memcpy(save.data() + kSaveChecksumOffset, &sum, 4);
    return save;
}

void Write(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                 static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("a well-formed save passes and reports its version id") {
    const auto save = MakeSave(0x35DA8175, 0x11);
    const auto check = CheckSave(save);
    CHECK(check.valid);
    CHECK(check.versionId == 0x35DA8175);
}

TEST_CASE("a single flipped byte, a truncated file and a foreign file are all rejected with a reason") {
    auto flipped = MakeSave(1, 0x22);
    flipped[0x1234] ^= 0x01;
    const auto bad = CheckSave(flipped);
    CHECK_FALSE(bad.valid);
    CHECK(bad.problem.find("the game will reject this save") != std::string::npos);

    auto truncated = MakeSave(1, 0x22);
    truncated.resize(truncated.size() - 100);
    CHECK(CheckSave(truncated).problem.find("truncated") != std::string::npos);

    std::vector<uint8_t> foreign(kSaveFileSize, 0);
    CHECK(CheckSave(foreign).problem == "does not start with BLOCK");
}

TEST_CASE("the checksum ignores its own four bytes, so rewriting it does not change the sum") {
    auto save = MakeSave(7, 0x33);
    const uint32_t before = ComputeSaveChecksum(save);
    std::memset(save.data() + kSaveChecksumOffset, 0xFF, 4);
    CHECK(ComputeSaveChecksum(save) == before);
}

TEST_CASE("backups skip unchanged saves and keep only the newest copies") {
    std::random_device random;
    const auto root = std::filesystem::temp_directory_path() / ("sacore_saves_" + std::to_string(random()));
    std::filesystem::create_directories(root);
    const auto slot = root / "GTASAsf1.b";
    const auto backups = root / "backups";
    std::string error;

    Write(slot, MakeSave(1, 0x01));
    CHECK(BackupSaveFile(slot, backups, "2026-10-13_10-00-00", 2, error));
    CHECK_FALSE(BackupSaveFile(slot, backups, "2026-10-13_10-00-01", 2, error));  // identical
    Write(slot, MakeSave(1, 0x02));
    CHECK(BackupSaveFile(slot, backups, "2026-10-13_10-00-02", 2, error));
    Write(slot, MakeSave(1, 0x03));
    const auto newest = BackupSaveFile(slot, backups, "2026-10-13_10-00-03", 2, error);
    REQUIRE(newest);

    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(backups / "GTASAsf1.b")) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"2026-10-13_10-00-02.b", "2026-10-13_10-00-03.b"});
    CHECK_FALSE(BackupSaveFile(root / "GTASAsf5.b", backups, "x", 2, error));  // no such slot
    std::filesystem::remove_all(root);
}

TEST_CASE("save names map to their files, story saves with up to three digits") {
    CHECK(ParseSaveName("slot8") == std::pair<std::string, int>{"GTASAsf", 8});
    CHECK(ParseSaveName("story7") == std::pair<std::string, int>{"GTASAst", 7});
    CHECK(ParseSaveName("story102") == std::pair<std::string, int>{"GTASAst", 102});
    CHECK_FALSE(ParseSaveName("slot9"));      // only 8 menu slots
    CHECK_FALSE(ParseSaveName("story121"));   // past the library
    CHECK_FALSE(ParseSaveName("story007"));   // one name per file
    CHECK_FALSE(ParseSaveName("story1a"));
    CHECK_FALSE(ParseSaveName("story"));
    CHECK_FALSE(ParseSaveName("slot10"));     // must not alias slot1
}
