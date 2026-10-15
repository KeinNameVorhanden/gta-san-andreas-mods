// View of the game's code in memory and the byte-signature checks used to validate addresses.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sacore {

struct ModuleImage {
    uintptr_t base = 0;                 // runtime base of the main module
    std::span<const uint8_t> text;      // main .text section as mapped (relocations applied)
    uintptr_t textRva = 0;              // RVA of text.data()

    // Bytes at an RVA if it lies inside .text, else empty.
    std::span<const uint8_t> At(uintptr_t rva, size_t size) const;
};

// The running game's image.
ModuleImage CurrentModuleImage();

// How often the 32-bit value appears in .text (an absolute operand referencing a global).
size_t CountReferences(const ModuleImage& image, uintptr_t value);

// Searches .text for prefix + <32-bit value> + suffix (e.g. "mov dword ptr [addr], 2").
bool ContainsInstruction(const ModuleImage& image, std::span<const uint8_t> prefix, uintptr_t value,
                         std::span<const uint8_t> suffix);

// Parses "55 8B EC" into bytes; returns empty on malformed input.
std::vector<uint8_t> ParseHexBytes(std::string_view text);
std::string ToHex(std::span<const uint8_t> bytes);

}  // namespace sacore
