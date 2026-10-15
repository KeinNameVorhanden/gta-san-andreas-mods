#include "build_info.h"

#include <doctest/doctest.h>

#include <vector>

using sacore::IdentifyExeBytes;

namespace {

// A minimal PE header: "MZ", e_lfanew = 0x80, "PE\0\0", file header with the given characteristics.
std::vector<unsigned char> FakeExe(unsigned short characteristics) {
    std::vector<unsigned char> bytes(0x200, 0xCC);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3C] = 0x80;
    bytes[0x3D] = bytes[0x3E] = bytes[0x3F] = 0;
    bytes[0x80] = 'P';
    bytes[0x81] = 'E';
    bytes[0x82] = bytes[0x83] = 0;
    bytes[0x80 + 4 + 18] = static_cast<unsigned char>(characteristics & 0xFF);
    bytes[0x80 + 4 + 19] = static_cast<unsigned char>(characteristics >> 8);
    return bytes;
}

}  // namespace

TEST_CASE("an executable with the 4 GB flag is still the same build, other changes are not") {
    const auto plain = IdentifyExeBytes(FakeExe(0x010F));         // 1.0 US header value
    const auto patched = IdentifyExeBytes(FakeExe(0x010F | 0x20));  // Large Address Aware set
    CHECK_FALSE(plain.largeAddressAware);
    CHECK(patched.largeAddressAware);
    CHECK(plain.sha256.size() == 64);
    CHECK(patched.sha256 == plain.sha256);

    const auto other = IdentifyExeBytes(FakeExe(0x010F | 0x2000));  // a DLL flag: a different file
    CHECK(other.sha256 != plain.sha256);

    auto modified = FakeExe(0x010F);
    modified[0x150] ^= 1;  // one byte of code changed
    CHECK(IdentifyExeBytes(modified).sha256 != plain.sha256);
}
