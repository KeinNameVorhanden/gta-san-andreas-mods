#include "stability/zip_writer.h"

#include <array>
#include <fstream>

namespace sacore {
namespace {

constexpr std::array<uint32_t, 256> MakeCrcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}

constexpr auto kCrcTable = MakeCrcTable();
constexpr size_t kChunk = 1 << 20;
// DOS date/time of 1980-01-01 00:00; the real time is part of the crash file names.
constexpr uint16_t kDosTime = 0;
constexpr uint16_t kDosDate = (1 << 5) | 1;

void Put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

struct Entry {
    const ZipSource* source;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
};

// One pass over a file for size and CRC (files are copied in a second pass, so a large dump never sits in memory).
bool Measure(const ZipSource& source, Entry& entry) {
    if (source.file.empty()) {
        entry.crc = Crc32(source.data);
        entry.size = static_cast<uint32_t>(source.data.size());
        return true;
    }
    std::ifstream in(source.file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<uint8_t> buffer(kChunk);
    uint32_t crc = 0;
    uint64_t total = 0;
    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<size_t>(in.gcount());
        crc = Crc32({buffer.data(), got}, crc);
        total += got;
    }
    if (total > 0xFFFFFFFFull) {
        return false;  // no ZIP64 here
    }
    entry.crc = crc;
    entry.size = static_cast<uint32_t>(total);
    return true;
}

std::vector<uint8_t> Header(uint32_t signature, const Entry& e, bool central) {
    std::vector<uint8_t> h;
    Put32(h, signature);
    if (central) {
        Put16(h, 20);  // version made by
    }
    Put16(h, 20);      // version needed
    Put16(h, 0x0800);  // UTF-8 names
    Put16(h, 0);       // stored
    Put16(h, kDosTime);
    Put16(h, kDosDate);
    Put32(h, e.crc);
    Put32(h, e.size);
    Put32(h, e.size);
    Put16(h, static_cast<uint16_t>(e.source->name.size()));
    Put16(h, 0);  // extra
    if (central) {
        Put16(h, 0);  // comment
        Put16(h, 0);  // disk
        Put16(h, 0);  // internal attributes
        Put32(h, 0);  // external attributes
        Put32(h, e.offset);
    }
    h.insert(h.end(), e.source->name.begin(), e.source->name.end());
    return h;
}

}  // namespace

uint32_t Crc32(std::span<const uint8_t> data, uint32_t crc) {
    crc = ~crc;
    for (const uint8_t byte : data) {
        crc = kCrcTable[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

bool WriteStoredZip(const std::filesystem::path& zip, const std::vector<ZipSource>& sources,
                    std::vector<std::string>* skipped) {
    std::vector<Entry> entries;
    for (const ZipSource& source : sources) {
        Entry entry{&source};
        if (Measure(source, entry)) {
            entries.push_back(entry);
        } else if (skipped) {
            skipped->push_back(source.name);
        }
    }
    std::ofstream out(zip, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    uint32_t offset = 0;
    std::vector<uint8_t> buffer(kChunk);
    for (Entry& entry : entries) {
        entry.offset = offset;
        const std::vector<uint8_t> header = Header(0x04034B50, entry, false);
        out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (entry.source->file.empty()) {
            out.write(reinterpret_cast<const char*>(entry.source->data.data()),
                      static_cast<std::streamsize>(entry.source->data.size()));
        } else {
            // Copy exactly the measured size, even if the file grew in between (the live log can).
            std::ifstream in(entry.source->file, std::ios::binary);
            uint32_t left = entry.size;
            while (left > 0 && in) {
                const auto want = static_cast<std::streamsize>(std::min<uint32_t>(left, kChunk));
                in.read(reinterpret_cast<char*>(buffer.data()), want);
                const auto got = static_cast<uint32_t>(in.gcount());
                out.write(reinterpret_cast<const char*>(buffer.data()), got);
                left -= got;
            }
            for (; left > 0; --left) {
                out.put('\0');  // the file shrank: keep the recorded size (CRC will not match; still a valid archive)
            }
        }
        offset += static_cast<uint32_t>(header.size()) + entry.size;
    }
    const uint32_t directoryOffset = offset;
    uint32_t directorySize = 0;
    for (const Entry& entry : entries) {
        const std::vector<uint8_t> header = Header(0x02014B50, entry, true);
        out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        directorySize += static_cast<uint32_t>(header.size());
    }
    std::vector<uint8_t> end;
    Put32(end, 0x06054B50);
    Put16(end, 0);
    Put16(end, 0);
    Put16(end, static_cast<uint16_t>(entries.size()));
    Put16(end, static_cast<uint16_t>(entries.size()));
    Put32(end, directorySize);
    Put32(end, directoryOffset);
    Put16(end, 0);
    out.write(reinterpret_cast<const char*>(end.data()), static_cast<std::streamsize>(end.size()));
    return static_cast<bool>(out);
}

}  // namespace sacore
