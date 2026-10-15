#include "version.h"

#include <charconv>
#include <format>

namespace sacore {
namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::string Version::ToString() const {
    return std::format("{}.{}.{}", major, minor, patch);
}

std::optional<Version> Version::Parse(std::string_view text) {
    text = Trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    int parts[3] = {0, 0, 0};
    int index = 0;
    const char* cursor = text.data();
    const char* end = text.data() + text.size();
    while (true) {
        if (index == 3 || cursor == end) {
            return std::nullopt;
        }
        const auto result = std::from_chars(cursor, end, parts[index]);
        if (result.ec != std::errc{} || parts[index] < 0) {
            return std::nullopt;
        }
        ++index;
        cursor = result.ptr;
        if (cursor == end) {
            break;
        }
        if (*cursor != '.') {
            return std::nullopt;
        }
        ++cursor;
    }
    return Version{parts[0], parts[1], parts[2]};
}

std::optional<VersionRequirement> VersionRequirement::Parse(std::string_view text) {
    VersionRequirement requirement;
    requirement.text_ = std::string(Trim(text));
    std::string_view rest = requirement.text_;
    if (rest.empty()) {
        return std::nullopt;
    }
    while (!rest.empty()) {
        const size_t comma = rest.find(',');
        std::string_view part = Trim(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        if (part == "*") {
            continue;
        }
        auto addUpperBound = [&](const Version& lower, const Version& upper) {
            requirement.comparators_.push_back({Op::GreaterEqual, lower});
            requirement.comparators_.push_back({Op::Less, upper});
        };
        if (part.starts_with('^') || part.starts_with('~')) {
            const char kind = part.front();
            const auto base = Version::Parse(part.substr(1));
            if (!base) {
                return std::nullopt;
            }
            if (kind == '~' || base->major == 0) {
                addUpperBound(*base, {base->major, base->minor + 1, 0});
            } else {
                addUpperBound(*base, {base->major + 1, 0, 0});
            }
            continue;
        }
        Op op = Op::Equal;
        if (part.starts_with(">=")) {
            op = Op::GreaterEqual;
            part.remove_prefix(2);
        } else if (part.starts_with("<=")) {
            op = Op::LessEqual;
            part.remove_prefix(2);
        } else if (part.starts_with('>')) {
            op = Op::Greater;
            part.remove_prefix(1);
        } else if (part.starts_with('<')) {
            op = Op::Less;
            part.remove_prefix(1);
        } else if (part.starts_with('=')) {
            part.remove_prefix(1);
        }
        const auto version = Version::Parse(part);
        if (!version) {
            return std::nullopt;
        }
        requirement.comparators_.push_back({op, *version});
    }
    return requirement;
}

bool VersionRequirement::IsSatisfiedBy(const Version& version) const {
    for (const Comparator& c : comparators_) {
        const bool ok = c.op == Op::Equal          ? version == c.version
                        : c.op == Op::Greater      ? version > c.version
                        : c.op == Op::GreaterEqual ? version >= c.version
                        : c.op == Op::Less         ? version < c.version
                                                   : version <= c.version;
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace sacore
