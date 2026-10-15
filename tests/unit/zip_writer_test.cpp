#include "stability/zip_writer.h"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using sacore::Crc32;
using sacore::WriteStoredZip;
using sacore::ZipSource;

namespace {

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}
uint16_t U16(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

}  // namespace

TEST_CASE("the CRC matches the standard check value") {
    const char* check = "123456789";
    CHECK(Crc32({reinterpret_cast<const uint8_t*>(check), 9}) == 0xCBF43926u);
    // Chunked computation equals one pass (the writer reads files in chunks).
    const uint32_t first = Crc32({reinterpret_cast<const uint8_t*>(check), 4});
    CHECK(Crc32({reinterpret_cast<const uint8_t*>(check) + 4, 5}, first) == 0xCBF43926u);
}

TEST_CASE("a crash package reads back entry by entry through its central directory, unreadable files skipped") {
    const auto dir = std::filesystem::temp_directory_path() / "sacore_zip_test";
    std::filesystem::create_directories(dir);
    const auto dump = dir / "crash.dmp";
    std::vector<uint8_t> dumpBytes(3 * 1024 * 1024 + 17);  // larger than one copy chunk
    for (size_t i = 0; i < dumpBytes.size(); ++i) {
        dumpBytes[i] = static_cast<uint8_t>(i * 31 + 7);
    }
    std::ofstream(dump, std::ios::binary).write(reinterpret_cast<const char*>(dumpBytes.data()),
                                                static_cast<std::streamsize>(dumpBytes.size()));
    const std::string report = "SACore crash report\n";
    std::vector<ZipSource> sources{{"report.txt", {}, {report.begin(), report.end()}},
                                   {"crash.dmp", dump, {}},
                                   {"config/missing.toml", dir / "does-not-exist.toml", {}},
                                   {"logs/empty.log", {}, {}}};
    std::vector<std::string> skipped;
    const auto zip = dir / "package.zip";
    REQUIRE(WriteStoredZip(zip, sources, &skipped));
    CHECK(skipped == std::vector<std::string>{"config/missing.toml"});

    std::ifstream in(zip, std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    REQUIRE(bytes.size() > 22);
    const size_t eocd = bytes.size() - 22;
    REQUIRE(U32(bytes, eocd) == 0x06054B50u);
    REQUIRE(U16(bytes, eocd + 10) == 3);
    size_t central = U32(bytes, eocd + 16);
    std::map<std::string, std::vector<uint8_t>> entries;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(U32(bytes, central) == 0x02014B50u);
        const uint32_t crc = U32(bytes, central + 16);
        const uint32_t size = U32(bytes, central + 20);
        const uint16_t nameLength = U16(bytes, central + 28);
        const uint32_t local = U32(bytes, central + 42);
        const std::string name(bytes.begin() + central + 46, bytes.begin() + central + 46 + nameLength);
        REQUIRE(U32(bytes, local) == 0x04034B50u);
        const size_t data = local + 30 + U16(bytes, local + 26) + U16(bytes, local + 28);
        std::vector<uint8_t> content(bytes.begin() + data, bytes.begin() + data + size);
        CHECK(Crc32(content) == crc);
        entries[name] = content;
        central += 46 + nameLength;
    }
    CHECK(entries["report.txt"] == std::vector<uint8_t>(report.begin(), report.end()));
    CHECK(entries["crash.dmp"] == dumpBytes);
    CHECK(entries["logs/empty.log"].empty());
    std::filesystem::remove_all(dir);
}
