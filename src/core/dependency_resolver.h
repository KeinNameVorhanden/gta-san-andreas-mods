// Decides which mods can load and in which order.
#pragma once

#include "mod_manifest.h"

#include <map>
#include <string>
#include <vector>

namespace sacore {

struct ResolverInput {
    std::vector<ModManifest> mods;
    Version coreVersion;
    int apiVersion = 0;
    std::string buildId;
};

struct ResolverResult {
    std::vector<std::string> loadOrder;                // ids of mods that may load, in order
    std::map<std::string, std::string> rejected;       // id -> human readable reason
    std::map<std::string, std::vector<std::string>> warnings;  // id -> non-fatal notes
};

// Rules:
// - duplicate ids: every copy is rejected.
// - api_version must equal the core API major; min_core_version must be <= core version;
//   target_builds (if present) must contain the running build.
// - a required dependency that is missing, rejected or has a non-matching version rejects the mod
//   (transitively); a missing or mismatching optional dependency only adds a warning.
// - order: dependencies and load_after targets first, load_before targets later;
//   ties are broken alphabetically so the order is deterministic.
// - mods in a dependency or ordering cycle are rejected with the cycle in the reason.
ResolverResult ResolveMods(const ResolverInput& input);

}  // namespace sacore
