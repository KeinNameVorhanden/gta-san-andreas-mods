#include "module_image.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace sacore {

std::span<const uint8_t> ModuleImage::At(uintptr_t rva, size_t size) const {
    if (rva < textRva || rva - textRva + size > text.size()) {
        return {};
    }
    return text.subspan(rva - textRva, size);
}

ModuleImage CurrentModuleImage() {
    ModuleImage image;
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    image.base = reinterpret_cast<uintptr_t>(base);
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 6) == 0) {
            image.text = {base + section->VirtualAddress, section->Misc.VirtualSize};
            image.textRva = section->VirtualAddress;
            break;
        }
    }
    return image;
}

size_t CountReferences(const ModuleImage& image, uintptr_t value) {
    uint8_t needle[4];
    const auto value32 = static_cast<uint32_t>(value);
    std::memcpy(needle, &value32, 4);
    size_t count = 0;
    auto it = image.text.begin();
    while ((it = std::search(it, image.text.end(), std::begin(needle), std::end(needle))) != image.text.end()) {
        ++count;
        ++it;
    }
    return count;
}

bool ContainsInstruction(const ModuleImage& image, std::span<const uint8_t> prefix, uintptr_t value,
                         std::span<const uint8_t> suffix) {
    std::vector<uint8_t> pattern(prefix.begin(), prefix.end());
    const auto value32 = static_cast<uint32_t>(value);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value32);
    pattern.insert(pattern.end(), bytes, bytes + 4);
    pattern.insert(pattern.end(), suffix.begin(), suffix.end());
    return std::search(image.text.begin(), image.text.end(), pattern.begin(), pattern.end()) != image.text.end();
}

std::vector<uint8_t> ParseHexBytes(std::string_view text) {
    std::vector<uint8_t> bytes;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        if (i + 1 >= text.size()) {
            return {};
        }
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<uint8_t>(high * 16 + low));
        i += 2;
    }
    return bytes;
}

std::string ToHex(std::span<const uint8_t> bytes) {
    std::string text;
    for (size_t i = 0; i < bytes.size(); ++i) {
        text += std::format("{}{:02X}", i ? " " : "", bytes[i]);
    }
    return text;
}

}  // namespace sacore
