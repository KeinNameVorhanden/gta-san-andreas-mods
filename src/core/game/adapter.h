// Per-build game adapter: validated addresses plus the capability model built on them.
#pragma once

#include "module_image.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

enum class SymbolCheck {
    CodeReferences,  // the absolute address appears at least `minReferences` times in .text
    Prologue,        // the function starts with `bytes` (or with E9 = hooked by another plugin)
    Instruction,     // .text contains `bytes` + <address> + `suffix`
};

struct SymbolSpec {
    std::string_view name;
    uintptr_t rva;
    SymbolCheck check;
    size_t minReferences = 0;
    std::string_view bytes;   // hex
    std::string_view suffix;  // hex, Instruction only
};

struct CapabilitySpec {
    std::string_view id;
    std::string_view description;
    std::span<const std::string_view> requiredSymbols;
    bool runtimeVerified;     // verified in a real runtime test on this build (docs/COMPATIBILITY.md)
};

struct BuildAdapterData {
    std::string_view buildId;
    std::span<const SymbolSpec> symbols;
    std::span<const CapabilitySpec> capabilities;
};

enum class CapabilityState { Supported, Experimental, Unsupported, NotInvestigated };
std::string_view ToString(CapabilityState state);

struct SymbolStatus {
    std::string name;
    uintptr_t address = 0;  // runtime address, 0 when the check failed
    bool ok = false;
    bool hookedByOther = false;
    std::string detail;
};

struct CapabilityStatus {
    std::string id;
    std::string description;
    CapabilityState state = CapabilityState::NotInvestigated;
    std::string reason;
};

class GameAdapter {
public:
    // Validates every symbol of `data` against `image`; capabilities whose symbols fail become Unsupported.
    GameAdapter(const BuildAdapterData& data, const ModuleImage& image);

    std::string_view BuildId() const { return buildId_; }
    // Runtime address of a validated symbol, or 0 if it is unknown or failed its check.
    uintptr_t Address(std::string_view symbol) const;
    CapabilityState State(std::string_view capability) const;
    bool Available(std::string_view capability) const {
        const auto s = State(capability);
        return s == CapabilityState::Supported || s == CapabilityState::Experimental;
    }
    const std::vector<SymbolStatus>& Symbols() const { return symbols_; }
    const std::vector<CapabilityStatus>& Capabilities() const { return capabilities_; }

private:
    std::string buildId_;
    std::vector<SymbolStatus> symbols_;
    std::vector<CapabilityStatus> capabilities_;
};

// True if `actual` starts with `signature` (hex bytes, "??" matches any byte).
bool MatchesSignature(std::span<const uint8_t> actual, std::string_view signature);

}  // namespace sacore
