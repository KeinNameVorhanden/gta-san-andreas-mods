#include "mod_loader.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <format>

namespace sacore {
namespace {

// The C host log callback has no user pointer, so it reaches the logger through this pointer.
// Exactly one ModLoader exists per process (owned by the core runtime).
Logger* g_hostLogger = nullptr;

void HostLog(const SacoreHost* host, SacoreLogLevel level, const char* message) {
    if (!g_hostLogger || !host || !message) {
        return;
    }
    const int clamped = std::clamp(static_cast<int>(level), 0, static_cast<int>(LogLevel::Error));
    g_hostLogger->Log(static_cast<LogLevel>(clamped), LogCategory::Mods, host->mod_id ? host->mod_id : "?", message);
}

// SEH guards: kept free of C++ objects with destructors, as __try requires.
int CallModMain(SacoreModMainFn main, const SacoreHost* host, SacoreModCallbacks* callbacks,
                unsigned long* exceptionCode) {
    __try {
        return main(host, callbacks);
    } __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return INT_MIN;
    }
}

bool CallShutdown(void (*shutdown)(void), unsigned long* exceptionCode) {
    __try {
        shutdown();
        return true;
    } __except (*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

std::string_view ToString(ModState state) {
    switch (state) {
        case ModState::Discovered: return "discovered";
        case ModState::Rejected: return "rejected";
        case ModState::Running: return "running";
        case ModState::Failed: return "failed";
        case ModState::Shutdown: return "shut down";
    }
    return "?";
}

ModLoader::ModLoader(Logger& logger, ModLoaderContext context) : logger_(logger), context_(std::move(context)) {
    g_hostLogger = &logger_;
}

ModLoader::~ModLoader() {
    ShutdownAll();
    g_hostLogger = nullptr;
}

void ModLoader::LoadAll(const std::filesystem::path& modsRoot) {
    std::error_code error;
    if (!std::filesystem::is_directory(modsRoot, error)) {
        logger_.Logf(LogLevel::Info, LogCategory::Mods, "", "no mods directory at {}, nothing to load", Utf8(modsRoot));
        return;
    }
    std::vector<std::filesystem::path> directories;
    for (const auto& entry : std::filesystem::directory_iterator(modsRoot, error)) {
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "mod.toml")) {
            directories.push_back(entry.path());
        }
    }
    std::sort(directories.begin(), directories.end());

    ResolverInput input;
    input.coreVersion = context_.coreVersion;
    input.apiVersion = SACORE_API_VERSION;
    input.buildId = context_.buildId;
    for (const auto& directory : directories) {
        auto record = std::make_unique<ModRecord>();
        record->directory = directory;
        record->id = Utf8(directory.filename());
        const ManifestResult manifest = LoadModManifest(directory / "mod.toml");
        if (!manifest.manifest) {
            record->state = ModState::Rejected;
            for (const std::string& e : manifest.errors) {
                record->reason += (record->reason.empty() ? "" : "; ") + e;
            }
            record->reason = "invalid mod.toml: " + record->reason;
        } else {
            record->manifest = manifest.manifest;
            record->id = manifest.manifest->id;
            input.mods.push_back(*manifest.manifest);
        }
        mods_.push_back(std::move(record));
    }

    const ResolverResult resolved = ResolveMods(input);
    for (auto& mod : mods_) {
        if (mod->state == ModState::Rejected) {
            continue;
        }
        if (auto rejected = resolved.rejected.find(mod->id); rejected != resolved.rejected.end()) {
            mod->state = ModState::Rejected;
            mod->reason = rejected->second;
        }
        if (auto warnings = resolved.warnings.find(mod->id); warnings != resolved.warnings.end()) {
            for (const std::string& warning : warnings->second) {
                logger_.Log(LogLevel::Warn, LogCategory::Mods, mod->id, warning);
            }
        }
    }
    for (const auto& mod : mods_) {
        if (mod->state == ModState::Rejected) {
            logger_.Logf(LogLevel::Error, LogCategory::Mods, mod->id, "not loaded: {} (directory {})", mod->reason,
                         Utf8(mod->directory));
        }
    }
    for (const std::string& id : resolved.loadOrder) {
        auto found = std::find_if(mods_.begin(), mods_.end(), [&](const auto& m) {
            return m->id == id && m->state == ModState::Discovered;
        });
        if (found != mods_.end()) {
            Initialize(**found);
        }
    }
    const auto running = std::count_if(mods_.begin(), mods_.end(),
                                       [](const auto& m) { return m->state == ModState::Running; });
    logger_.Logf(LogLevel::Info, LogCategory::Mods, "", "{} of {} mod(s) running", running, mods_.size());
}

void ModLoader::Initialize(ModRecord& mod) {
    const ModManifest& manifest = *mod.manifest;
    for (const auto& [depId, requirement] : manifest.dependencies) {
        auto dep = std::find_if(mods_.begin(), mods_.end(), [&](const auto& m) { return m->id == depId; });
        if (dep == mods_.end() || (*dep)->state != ModState::Running) {
            mod.state = ModState::Failed;
            mod.reason = std::format("required mod '{}' is not running", depId);
            logger_.Logf(LogLevel::Error, LogCategory::Mods, mod.id, "not started: {}", mod.reason);
            return;
        }
    }
    if (manifest.dll.empty()) {
        mod.state = ModState::Running;
        loadOrder_.push_back(&mod);
        logger_.Logf(LogLevel::Info, LogCategory::Mods, mod.id, "running ({} {}, content only)", manifest.name,
                     manifest.version.ToString());
        return;
    }
    const std::filesystem::path dll = mod.directory / manifest.dll;
    mod.module = LoadLibraryW(dll.c_str());
    if (!mod.module) {
        mod.state = ModState::Failed;
        mod.reason = std::format("LoadLibrary failed for {} (Windows error {})", Utf8(dll), GetLastError());
        logger_.Log(LogLevel::Error, LogCategory::Mods, mod.id, mod.reason);
        return;
    }
    const auto main = reinterpret_cast<SacoreModMainFn>(GetProcAddress(mod.module, SACORE_MOD_MAIN_NAME));
    if (!main) {
        mod.state = ModState::Failed;
        mod.reason = std::format("{} does not export {}", manifest.dll, SACORE_MOD_MAIN_NAME);
        logger_.Log(LogLevel::Error, LogCategory::Mods, mod.id, mod.reason);
        FreeLibrary(mod.module);
        mod.module = nullptr;
        return;
    }
    if (!manifest.configSchema.empty()) {
        ConfigSchema schema;
        schema.settings = manifest.configSchema;
        mod.configFile = context_.configDirectory / (mod.id + ".toml");
        const ConfigLoadResult config = ConfigLoader::Load(mod.configFile, schema);
        for (const std::string& problem : config.problems) {
            logger_.Log(LogLevel::Warn, LogCategory::Configuration, mod.id, problem);
        }
        mod.config = config.config;
    }
    mod.idText = mod.id;
    mod.directoryText = Utf8(mod.directory);
    mod.host = SacoreHost{};
    mod.host.struct_size = sizeof(SacoreHost);
    mod.host.api_version = SACORE_API_VERSION;
    mod.host.core_version = context_.coreVersionText.c_str();
    mod.host.game_build = context_.buildId.c_str();
    mod.host.mod_id = mod.idText.c_str();
    mod.host.mod_directory = mod.directoryText.c_str();
    mod.host.core_internal = &mod;
    mod.host.log = &HostLog;
    if (context_.fillHost) {
        context_.fillHost(mod, mod.host);
    }
    mod.callbacks = SacoreModCallbacks{sizeof(SacoreModCallbacks), nullptr};

    unsigned long exceptionCode = 0;
    const int result = CallModMain(main, &mod.host, &mod.callbacks, &exceptionCode);
    if (exceptionCode != 0) {
        // The DLL stays mapped: its state is unknown after a crash and unloading could run broken code.
        MarkFailed(mod, std::format("crashed in {} with exception {:#010x}; mod disabled for this session",
                                    SACORE_MOD_MAIN_NAME, exceptionCode));
        return;
    }
    if (result != 0) {
        MarkFailed(mod, std::format("{} returned error code {}", SACORE_MOD_MAIN_NAME, result));
        return;
    }
    mod.state = ModState::Running;
    loadOrder_.push_back(&mod);
    logger_.Logf(LogLevel::Info, LogCategory::Mods, mod.id, "running ({} {}, {})", manifest.name,
                 manifest.version.ToString(), Utf8(dll.filename()));
}

void ModLoader::MarkFailed(ModRecord& mod, const std::string& reason) {
    if (mod.state == ModState::Failed) {
        return;
    }
    mod.state = ModState::Failed;
    mod.reason = reason;
    logger_.Log(LogLevel::Error, LogCategory::Mods, mod.id, reason);
    if (context_.onModStopped) {
        context_.onModStopped(mod);
    }
    std::erase(loadOrder_, &mod);
}

void ModLoader::ShutdownAll() {
    for (auto it = loadOrder_.rbegin(); it != loadOrder_.rend(); ++it) {
        ModRecord& mod = **it;
        if (mod.state != ModState::Running) {
            continue;
        }
        if (mod.callbacks.shutdown) {
            unsigned long exceptionCode = 0;
            if (!CallShutdown(mod.callbacks.shutdown, &exceptionCode)) {
                logger_.Logf(LogLevel::Error, LogCategory::Mods, mod.id, "crashed in shutdown with exception {:#010x}",
                             exceptionCode);
            }
        }
        mod.state = ModState::Shutdown;
        if (context_.onModStopped) {
            context_.onModStopped(mod);
        }
        logger_.Log(LogLevel::Info, LogCategory::Mods, mod.id, "shut down");
    }
    loadOrder_.clear();
}

}  // namespace sacore
