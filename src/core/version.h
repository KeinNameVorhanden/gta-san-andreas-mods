// Semantic versions (major.minor.patch) and version requirements used by mod manifests.
#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const Version&) const = default;
    std::string ToString() const;

    // Accepts "1", "1.2" and "1.2.3"; missing parts are 0. Rejects anything else.
    static std::optional<Version> Parse(std::string_view text);
};

// A requirement is a comma-separated list of comparators that must all hold:
//   "*", "1.2.3" or "=1.2.3", ">1.2", ">=1.2", "<2", "<=2.1",
//   "^1.2.3" (same major, >= 1.2.3; for major 0: same minor), "~1.2.3" (same major.minor, >= 1.2.3).
class VersionRequirement {
public:
    static std::optional<VersionRequirement> Parse(std::string_view text);
    bool IsSatisfiedBy(const Version& version) const;
    const std::string& Text() const { return text_; }

private:
    enum class Op { Equal, Greater, GreaterEqual, Less, LessEqual };
    struct Comparator {
        Op op;
        Version version;
    };
    std::vector<Comparator> comparators_;
    std::string text_;
};

}  // namespace sacore
