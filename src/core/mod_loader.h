// Discovers mods under sacore\mods, resolves them, loads their DLLs and runs their lifecycle.
#pragma once

#include "config.h"
#include "dependency_resolver.h"
#include "logger.h"
#include "mod_manifest.h"

#include <sacore/sacore_api.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct HINSTANCE__;

namespace sacore {

enum class ModState { Discovered, Rejected, Running, Failed, Shutdown };
std::string_view ToString(ModState state);

struct ModRecord {
    std::filesystem::path directory;
    std::string id;  // manifest id, or the directory name if the manifest is unusable
    std::optional<ModManifest> manifest;
    ModState state = ModState::Discovered;
    std::string reason;  // why Rejected or Failed
    HINSTANCE__* module = nullptr;
    Config config;       // values of the mod's declared [config] settings
    std::filesystem::path configFile;  // sacore\config\mods\<id>.toml (live settings write it back)
    // Storage behind the host struct handed to the mod.
    std::string idText;
    std::string directoryText;
    SacoreHost host{};
    SacoreModCallbacks callbacks{};
};

struct ModLoaderContext {
    Version coreVersion;
    std::string coreVersionText;
    std::string buildId;
    std::filesystem::path configDirectory;  // sacore\config\mods
    // Fills the API function table of a mod's host struct (set by the core runtime).
    std::function<void(ModRecord&, SacoreHost&)> fillHost;
    // Called when a mod stops (failure or shutdown) so the core can drop its subscriptions and filters.
    std::function<void(ModRecord&)> onModStopped;
};

class ModLoader {
public:
    ModLoader(Logger& logger, ModLoaderContext context);
    ~ModLoader();
    ModLoader(const ModLoader&) = delete;
    ModLoader& operator=(const ModLoader&) = delete;

    // Scans <modsRoot>/<dir>/mod.toml, resolves, loads and initializes mods in dependency order.
    void LoadAll(const std::filesystem::path& modsRoot);
    // Calls shutdown callbacks in reverse load order. Safe to call twice.
    void ShutdownAll();
    // Disables a running mod after a crash in one of its callbacks.
    void MarkFailed(ModRecord& mod, const std::string& reason);
    const std::vector<std::unique_ptr<ModRecord>>& Mods() const { return mods_; }
    // Running mods in load order.
    const std::vector<ModRecord*>& LoadOrder() const { return loadOrder_; }

private:
    void Initialize(ModRecord& mod);
    Logger& logger_;
    ModLoaderContext context_;
    std::vector<std::unique_ptr<ModRecord>> mods_;
    std::vector<ModRecord*> loadOrder_;
};

}  // namespace sacore
