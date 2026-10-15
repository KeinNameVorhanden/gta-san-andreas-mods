#include "dependency_resolver.h"

#include <algorithm>
#include <format>
#include <set>

namespace sacore {
namespace {

using Graph = std::map<std::string, std::set<std::string>>;  // node -> nodes that must come after it

void RejectByRequiredDependencies(const std::map<std::string, const ModManifest*>& candidates,
                                  std::map<std::string, std::string>& rejected) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [id, mod] : candidates) {
            if (rejected.contains(id)) {
                continue;
            }
            for (const auto& [depId, requirement] : mod->dependencies) {
                std::string reason;
                const auto found = candidates.find(depId);
                if (found == candidates.end()) {
                    reason = std::format("requires '{}' {} which is not installed", depId, requirement.Text());
                } else if (auto r = rejected.find(depId); r != rejected.end()) {
                    reason = std::format("requires '{}' which failed: {}", depId, r->second);
                } else if (!requirement.IsSatisfiedBy(found->second->version)) {
                    reason = std::format("requires '{}' {} but {} is installed", depId, requirement.Text(),
                                         found->second->version.ToString());
                }
                if (!reason.empty()) {
                    rejected[id] = reason;
                    changed = true;
                    break;
                }
            }
        }
    }
}

Graph BuildGraph(const std::map<std::string, const ModManifest*>& accepted, ResolverResult& result) {
    Graph graph;
    for (const auto& [id, mod] : accepted) {
        graph[id];
    }
    for (const auto& [id, mod] : accepted) {
        for (const auto& [depId, requirement] : mod->dependencies) {
            graph[depId].insert(id);
        }
        for (const auto& [depId, requirement] : mod->optionalDependencies) {
            const auto found = accepted.find(depId);
            if (found == accepted.end()) {
                result.warnings[id].push_back(std::format("optional dependency '{}' not available", depId));
            } else if (!requirement.IsSatisfiedBy(found->second->version)) {
                result.warnings[id].push_back(std::format("optional dependency '{}' {} not satisfied by {}, ignored",
                                                          depId, requirement.Text(),
                                                          found->second->version.ToString()));
            } else {
                graph[depId].insert(id);
            }
        }
        for (const std::string& target : mod->loadAfter) {
            if (accepted.contains(target)) {
                graph[target].insert(id);
            }
        }
        for (const std::string& target : mod->loadBefore) {
            if (accepted.contains(target)) {
                graph[id].insert(target);
            }
        }
    }
    return graph;
}

// Kahn's algorithm with alphabetical tie-breaking. Returns nodes left over (part of or behind a cycle).
std::set<std::string> TopologicalOrder(const Graph& graph, std::vector<std::string>& order) {
    std::map<std::string, int> incoming;
    for (const auto& [node, successors] : graph) {
        incoming[node];
        for (const std::string& next : successors) {
            ++incoming[next];
        }
    }
    std::set<std::string> ready;
    for (const auto& [node, count] : incoming) {
        if (count == 0) {
            ready.insert(node);
        }
    }
    while (!ready.empty()) {
        const std::string node = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(node);
        for (const std::string& next : graph.at(node)) {
            if (--incoming[next] == 0) {
                ready.insert(next);
            }
        }
    }
    std::set<std::string> leftover;
    for (const auto& [node, count] : incoming) {
        if (count > 0) {
            leftover.insert(node);
        }
    }
    return leftover;
}

// Finds one cycle among `leftover` nodes, as {a, b, a} meaning "a -> b -> a".
// Every leftover node still has a leftover predecessor (otherwise Kahn would have emitted it), so walking
// predecessors must revisit a node; nodes merely downstream of a cycle are never part of the result.
std::vector<std::string> FindCycle(const Graph& graph, const std::set<std::string>& leftover) {
    std::map<std::string, std::string> predecessor;
    for (const auto& [node, successors] : graph) {
        if (!leftover.contains(node)) {
            continue;
        }
        for (const std::string& next : successors) {
            if (leftover.contains(next)) {
                predecessor.emplace(next, node);
            }
        }
    }
    std::vector<std::string> path;
    std::string node = *leftover.begin();
    while (std::find(path.begin(), path.end(), node) == path.end()) {
        path.push_back(node);
        node = predecessor.at(node);
    }
    // path is in predecessor order; the cycle starts at the first repeat of `node`.
    std::vector<std::string> cycle(std::find(path.begin(), path.end(), node), path.end());
    std::reverse(cycle.begin(), cycle.end());
    cycle.push_back(cycle.front());
    return cycle;
}

}  // namespace

ResolverResult ResolveMods(const ResolverInput& input) {
    ResolverResult result;

    std::map<std::string, int> idCount;
    for (const ModManifest& mod : input.mods) {
        ++idCount[mod.id];
    }
    std::map<std::string, const ModManifest*> candidates;
    for (const ModManifest& mod : input.mods) {
        if (idCount[mod.id] > 1) {
            result.rejected[mod.id] = std::format("{} mods share the id '{}'", idCount[mod.id], mod.id);
            continue;
        }
        if (mod.apiVersion != input.apiVersion) {
            result.rejected[mod.id] =
                std::format("built for mod API {}, this core provides API {}", mod.apiVersion, input.apiVersion);
        } else if (mod.minCoreVersion && *mod.minCoreVersion > input.coreVersion) {
            result.rejected[mod.id] = std::format("needs core {} or newer, this core is {}",
                                                  mod.minCoreVersion->ToString(), input.coreVersion.ToString());
        } else if (!mod.targetBuilds.empty() &&
                   std::find(mod.targetBuilds.begin(), mod.targetBuilds.end(), input.buildId) ==
                       mod.targetBuilds.end()) {
            result.rejected[mod.id] = std::format("does not support game build '{}'", input.buildId);
        }
        candidates[mod.id] = &mod;
    }

    while (true) {
        RejectByRequiredDependencies(candidates, result.rejected);
        std::map<std::string, const ModManifest*> accepted;
        for (const auto& [id, mod] : candidates) {
            if (!result.rejected.contains(id)) {
                accepted[id] = mod;
            }
        }
        result.warnings.clear();
        const Graph graph = BuildGraph(accepted, result);
        std::vector<std::string> order;
        const std::set<std::string> leftover = TopologicalOrder(graph, order);
        if (leftover.empty()) {
            result.loadOrder = std::move(order);
            return result;
        }
        const std::vector<std::string> cycle = FindCycle(graph, leftover);
        std::string text;
        for (const std::string& node : cycle) {
            text += (text.empty() ? "" : " -> ") + node;
        }
        for (size_t i = 0; i + 1 < cycle.size(); ++i) {
            result.rejected[cycle[i]] = "dependency or load order cycle: " + text;
        }
    }
}

}  // namespace sacore
