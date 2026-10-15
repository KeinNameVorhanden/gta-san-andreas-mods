#include "adapter.h"

#include <algorithm>
#include <format>

namespace sacore {

std::string_view ToString(CapabilityState state) {
    switch (state) {
        case CapabilityState::Supported: return "supported";
        case CapabilityState::Experimental: return "experimental";
        case CapabilityState::Unsupported: return "unsupported";
        case CapabilityState::NotInvestigated: return "not investigated";
    }
    return "?";
}

namespace {

// Signature tokens are hex bytes or "??" for a byte that may differ (for example a relocated address operand).
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> fixed;
};

Pattern ParsePattern(std::string_view text) {
    Pattern pattern;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        const std::string_view token = text.substr(i, 2);
        if (token == "??") {
            pattern.bytes.push_back(0);
            pattern.fixed.push_back(false);
        } else {
            const std::vector<uint8_t> byte = ParseHexBytes(token);
            if (byte.size() != 1) {
                return {};
            }
            pattern.bytes.push_back(byte[0]);
            pattern.fixed.push_back(true);
        }
        i += 2;
    }
    return pattern;
}

bool Matches(std::span<const uint8_t> actual, const Pattern& pattern) {
    for (size_t i = 0; i < pattern.bytes.size(); ++i) {
        if (pattern.fixed[i] && actual[i] != pattern.bytes[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool MatchesSignature(std::span<const uint8_t> actual, std::string_view signature) {
    const Pattern pattern = ParsePattern(signature);
    return !pattern.bytes.empty() && actual.size() >= pattern.bytes.size() && Matches(actual, pattern);
}

namespace {

SymbolStatus Check(const SymbolSpec& spec, const ModuleImage& image) {
    SymbolStatus status;
    status.name = spec.name;
    const uintptr_t address = image.base + spec.rva;
    switch (spec.check) {
        case SymbolCheck::CodeReferences: {
            const size_t count = CountReferences(image, address);
            status.ok = count >= spec.minReferences;
            status.detail = std::format("{} code references (need {})", count, spec.minReferences);
            break;
        }
        case SymbolCheck::Prologue: {
            const Pattern expected = ParsePattern(spec.bytes);
            const std::span<const uint8_t> actual = image.At(spec.rva, expected.bytes.size());
            if (actual.size() != expected.bytes.size() || expected.bytes.empty()) {
                status.detail = "function outside the code section";
            } else if (actual[0] == 0xE9) {
                status.ok = true;
                status.hookedByOther = true;
                status.detail = "starts with JMP: already hooked by another plugin, will be chained";
            } else if (Matches(actual, expected)) {
                status.ok = true;
                status.detail = "prologue matches";
            } else {
                status.detail = std::format("prologue mismatch: expected {}, found {}", spec.bytes, ToHex(actual));
            }
            break;
        }
        case SymbolCheck::Instruction: {
            status.ok = ContainsInstruction(image, ParseHexBytes(spec.bytes), address, ParseHexBytes(spec.suffix));
            status.detail = status.ok ? "reference instruction found" : "reference instruction NOT found";
            break;
        }
    }
    status.address = status.ok ? address : 0;
    return status;
}

}  // namespace

GameAdapter::GameAdapter(const BuildAdapterData& data, const ModuleImage& image) : buildId_(data.buildId) {
    for (const SymbolSpec& spec : data.symbols) {
        symbols_.push_back(Check(spec, image));
    }
    for (const CapabilitySpec& spec : data.capabilities) {
        CapabilityStatus status{std::string(spec.id), std::string(spec.description), CapabilityState::Supported, {}};
        for (std::string_view required : spec.requiredSymbols) {
            const auto symbol = std::find_if(symbols_.begin(), symbols_.end(),
                                             [&](const SymbolStatus& s) { return s.name == required; });
            if (symbol == symbols_.end()) {
                status.state = CapabilityState::Unsupported;
                status.reason = std::format("symbol '{}' is not mapped for this build", required);
                break;
            }
            if (!symbol->ok) {
                status.state = CapabilityState::Unsupported;
                status.reason = std::format("symbol '{}' failed its check: {}", required, symbol->detail);
                break;
            }
        }
        if (status.state == CapabilityState::Supported && !spec.runtimeVerified) {
            status.state = CapabilityState::Experimental;
            status.reason = "addresses validated, behaviour not yet verified in a runtime test on this build";
        }
        capabilities_.push_back(std::move(status));
    }
}

uintptr_t GameAdapter::Address(std::string_view symbol) const {
    const auto found =
        std::find_if(symbols_.begin(), symbols_.end(), [&](const SymbolStatus& s) { return s.name == symbol; });
    return found == symbols_.end() ? 0 : found->address;
}

CapabilityState GameAdapter::State(std::string_view capability) const {
    const auto found = std::find_if(capabilities_.begin(), capabilities_.end(),
                                    [&](const CapabilityStatus& c) { return c.id == capability; });
    return found == capabilities_.end() ? CapabilityState::NotInvestigated : found->state;
}

}  // namespace sacore
