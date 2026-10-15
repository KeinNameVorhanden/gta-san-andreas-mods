#include "game/adapter.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string_view>
#include <vector>

using sacore::BuildAdapterData;
using sacore::CapabilitySpec;
using sacore::CapabilityState;
using sacore::GameAdapter;
using sacore::ModuleImage;
using sacore::SymbolCheck;
using sacore::SymbolSpec;

namespace {

constexpr uintptr_t kBase = 0x00400000;
constexpr uintptr_t kTextRva = 0x1000;

// A fake .text section: code at RVA 0x1000.. with chosen bytes written at offsets.
struct FakeCode {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x400, 0xCC);
    void Put(size_t offset, std::initializer_list<uint8_t> data) { std::copy(data.begin(), data.end(), bytes.begin() + offset); }
    void PutAddress(size_t offset, uint32_t address) { std::memcpy(bytes.data() + offset, &address, 4); }
    ModuleImage Image() const { return ModuleImage{kBase, bytes, kTextRva}; }
};

constexpr std::string_view kState[] = {"state"};
constexpr std::string_view kStateAndSave[] = {"state", "save"};
constexpr std::string_view kCounter[] = {"counter"};
constexpr std::string_view kUnmapped[] = {"not_in_this_build"};

constexpr CapabilitySpec kCaps[] = {
    {"read_state", "", kState, true},
    {"save_events", "", kStateAndSave, true},
    {"counter", "", kCounter, false},
    {"engine_only", "", {}, true},
    {"needs_unmapped", "", kUnmapped, true},
};

}  // namespace

TEST_CASE("one failing symbol disables only the capabilities that need it") {
    FakeCode code;
    // "mov dword ptr [0x00500000], 2" for the state symbol (RVA 0x100000).
    code.Put(0x10, {0xC7, 0x05});
    code.PutAddress(0x12, kBase + 0x100000);
    code.Put(0x16, {0x02, 0x00, 0x00, 0x00});
    // Save function at RVA 0x1200 with a prologue that does NOT match the expected one.
    code.Put(0x200, {0x55, 0x8B, 0xEC, 0x90});
    const SymbolSpec symbols[] = {
        {"state", 0x100000, SymbolCheck::Instruction, 0, "C7 05", "02 00 00 00"},
        {"save", 0x1200, SymbolCheck::Prologue, 0, "55 8B EC 81", ""},
        {"counter", 0x100100, SymbolCheck::CodeReferences, 1, "", ""},
    };
    code.PutAddress(0x300, kBase + 0x100100);
    const GameAdapter adapter(BuildAdapterData{"fake", symbols, kCaps}, code.Image());

    CHECK(adapter.State("read_state") == CapabilityState::Supported);
    CHECK(adapter.State("save_events") == CapabilityState::Unsupported);
    CHECK(adapter.State("counter") == CapabilityState::Experimental);  // valid but not runtime verified
    CHECK(adapter.State("engine_only") == CapabilityState::Supported);
    CHECK(adapter.State("needs_unmapped") == CapabilityState::Unsupported);
    CHECK(adapter.State("never_listed") == CapabilityState::NotInvestigated);
    CHECK(adapter.Address("state") == kBase + 0x100000);
    CHECK(adapter.Address("save") == 0);
    const auto& save = adapter.Capabilities()[1];
    CHECK(save.reason.find("prologue mismatch: expected 55 8B EC 81, found 55 8B EC 90") != std::string::npos);
}

TEST_CASE("a function already hooked by another plugin is accepted and flagged") {
    FakeCode code;
    code.Put(0x200, {0xE9, 0x11, 0x22, 0x33});
    const SymbolSpec symbols[] = {{"save", 0x1200, SymbolCheck::Prologue, 0, "55 8B EC 81", ""}};
    const GameAdapter adapter(BuildAdapterData{"fake", symbols, {}}, code.Image());
    REQUIRE(adapter.Symbols().size() == 1);
    CHECK(adapter.Symbols()[0].ok);
    CHECK(adapter.Symbols()[0].hookedByOther);
}

TEST_CASE("relocation matters: a reference to the preferred-base address does not validate a relocated image") {
    FakeCode code;
    code.PutAddress(0x40, 0x00400000 + 0x100100);  // operand as in the file (base 0x400000)
    const SymbolSpec symbols[] = {{"counter", 0x100100, SymbolCheck::CodeReferences, 1, "", ""}};
    ModuleImage relocated = code.Image();
    relocated.base = 0x009F0000;  // loaded elsewhere but operands were not relocated
    const GameAdapter adapter(BuildAdapterData{"fake", symbols, kCaps}, relocated);
    CHECK_FALSE(adapter.Symbols()[0].ok);
    CHECK(adapter.State("counter") == CapabilityState::Unsupported);
}

TEST_CASE("functions outside the code section fail instead of reading out of bounds") {
    FakeCode code;
    const SymbolSpec symbols[] = {{"far", 0x900000, SymbolCheck::Prologue, 0, "55 8B EC", ""}};
    const GameAdapter adapter(BuildAdapterData{"fake", symbols, {}}, code.Image());
    CHECK_FALSE(adapter.Symbols()[0].ok);
    CHECK(adapter.Symbols()[0].detail == "function outside the code section");
}

TEST_CASE("wildcard signatures accept a relocated operand and nothing else") {
    // newsteam RpHAnim frame destructor: mov eax,[0xD27D88] at the preferred base; ASLR changes those four bytes.
    const std::vector<uint8_t> preferred = {0xA1, 0x88, 0x7D, 0xD2, 0x00, 0x8B, 0x54, 0x24, 0x04, 0x53, 0x55, 0x8D, 0x2C, 0x10};
    std::vector<uint8_t> relocated = preferred;
    relocated[3] = 0x37;  // image loaded at 0x009F0000 instead of 0x00400000
    const char* signature = "A1 ?? ?? ?? ?? 8B 54 24 04 53 55 8D 2C 10";
    CHECK(sacore::MatchesSignature(preferred, signature));
    CHECK(sacore::MatchesSignature(relocated, signature));
    std::vector<uint8_t> otherFunction = preferred;
    otherFunction[10] = 0x56;  // push esi instead of push ebp
    CHECK_FALSE(sacore::MatchesSignature(otherFunction, signature));
    CHECK_FALSE(sacore::MatchesSignature(std::span(preferred).first(8), signature));  // truncated code
    CHECK_FALSE(sacore::MatchesSignature(preferred, "A1 ?"));                          // malformed signature
}
