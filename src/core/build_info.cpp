#include "build_info.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <format>
#include <vector>

namespace sacore {

namespace {

std::string HexSha256(const unsigned char* data, size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    unsigned char digest[32];
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<unsigned char*>(data), static_cast<ULONG>(size), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
        for (unsigned char byte : digest) {
            result += std::format("{:02x}", byte);
        }
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return result;
}

}  // namespace

ExeIdentity IdentifyExeBytes(std::vector<unsigned char> bytes) {
    ExeIdentity identity;
    // IMAGE_FILE_HEADER.Characteristics: e_lfanew (0x3C) + "PE\0\0" (4) + 18 bytes into the file header.
    if (bytes.size() >= 0x40) {
        const uint32_t pe = static_cast<uint32_t>(bytes[0x3C]) | static_cast<uint32_t>(bytes[0x3D]) << 8 |
                            static_cast<uint32_t>(bytes[0x3E]) << 16 | static_cast<uint32_t>(bytes[0x3F]) << 24;
        const size_t at = static_cast<size_t>(pe) + 4 + 18;
        if (at + 2 <= bytes.size() && bytes[pe] == 'P' && bytes[pe + 1] == 'E') {
            identity.largeAddressAware = (bytes[at] & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
            bytes[at] = static_cast<unsigned char>(bytes[at] & ~IMAGE_FILE_LARGE_ADDRESS_AWARE);
        }
    }
    identity.sha256 = HexSha256(bytes.data(), bytes.size());
    return identity;
}

ExeIdentity IdentifyExe(const std::filesystem::path& exe) {
    FILE* stream = nullptr;
    if (_wfopen_s(&stream, exe.c_str(), L"rb") != 0 || !stream) {
        return {};
    }
    std::vector<unsigned char> bytes;
    std::vector<unsigned char> chunk(1 << 20);
    size_t read = 0;
    while ((read = std::fread(chunk.data(), 1, chunk.size(), stream)) > 0) {
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read));
    }
    std::fclose(stream);
    return bytes.empty() ? ExeIdentity{} : IdentifyExeBytes(std::move(bytes));
}

std::string Sha256OfFile(const std::filesystem::path& file) {
    FILE* stream = nullptr;
    if (_wfopen_s(&stream, file.c_str(), L"rb") != 0 || !stream) {
        return {};
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> chunk(1 << 20);
        bool ok = true;
        size_t read = 0;
        while (ok && (read = std::fread(chunk.data(), 1, chunk.size(), stream)) > 0) {
            ok = BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(), static_cast<ULONG>(read), 0));
        }
        unsigned char digest[32];
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            for (unsigned char byte : digest) {
                result += std::format("{:02x}", byte);
            }
        }
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    std::fclose(stream);
    return result;
}

std::optional<BuildInfo> FindBuildBySha256(std::string_view sha256) {
    for (const BuildInfo& build : kKnownBuilds) {
        if (build.sha256 == sha256) {
            return build;
        }
    }
    return std::nullopt;
}

}  // namespace sacore
