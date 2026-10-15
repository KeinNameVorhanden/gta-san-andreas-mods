// Known game builds and executable identification.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

struct BuildInfo {
    std::string_view id;
    std::string_view sha256;
    std::string_view description;
};

// Same data as tools/data/known_builds.json.
inline constexpr BuildInfo kKnownBuilds[] = {
    {"newsteam_r2_lv", "aa07fcdc8723a2831f77a35491dea6cdd58f6733e3ac69a10ddf5cdf95034fe3",
     "Steam newsteam r2, German low-violence (2014-12-01)"},
    {"us10_hoodlum", "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26",
     "1.0 US Hoodlum (2005-04-28)"},
};

// SHA-256 of a file as lowercase hex; empty on failure (Windows CNG).
std::string Sha256OfFile(const std::filesystem::path& file);

// The game executable's identity: the SHA-256 of the file with the Large Address Aware flag cleared, so a copy with
// the 4 GB flag set (tools/laa.py) is still its build; `largeAddressAware` says whether the flag is set.
struct ExeIdentity {
    std::string sha256;  // empty on failure
    bool largeAddressAware = false;
};
ExeIdentity IdentifyExe(const std::filesystem::path& exe);
// The same for bytes already read (unit tested).
ExeIdentity IdentifyExeBytes(std::vector<unsigned char> bytes);

std::optional<BuildInfo> FindBuildBySha256(std::string_view sha256);

}  // namespace sacore
