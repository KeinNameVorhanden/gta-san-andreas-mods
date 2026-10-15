// Owns the core services for one game process.
#pragma once

#include "config.h"
#include "content/content_service.h"
#include "engine/engine.h"
#include "event_bus.h"
#include "game/adapter.h"
#include "game/game.h"
#include "logger.h"
#include "mod_api.h"
#include "mod_loader.h"
#include "patch_manager.h"
#include "saves/save_service.h"
#include "stability/stability.h"
#include "testing/harness.h"

#include <windows.h>
#include <d3d9.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace sacore {

class CoreRuntime {
public:
    explicit CoreRuntime(HMODULE self);

    // Game thread, first Direct3DCreate9 call: logging, config, build detection, adapter, features, mods.
    void Initialize();
    // Every IDirect3D9 the game creates.
    void OnDirect3DCreated(IDirect3D9* d3d);

    static ConfigSchema Schema();

private:
    void WriteCapabilityReport() const;
    void OnGameWindowDestroyed();
    std::string CrashContext() const;
    void WritePatchReport() const;

    HMODULE self_;
    std::filesystem::path gameDirectory_;
    std::filesystem::path dataDirectory_;  // <game>\sacore
    std::unique_ptr<Logger> logger_;
    RingBufferSink* recentLog_ = nullptr;  // owned by logger_
    std::unique_ptr<EventBus> bus_;
    std::unique_ptr<PatchManager> patches_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Stability> stability_;
    std::unique_ptr<GameAdapter> adapter_;
    std::unique_ptr<Game> game_;
    std::unique_ptr<SaveService> saves_;
    std::unique_ptr<ModApi> modApi_;
    std::unique_ptr<ModLoader> mods_;
    std::unique_ptr<ContentService> content_;
    std::unique_ptr<Harness> harness_;
    std::filesystem::path logFile_;
    std::string coreSha256_;
    ULONGLONG startTick_ = 0;
    std::optional<std::string> buildId_;
};

}  // namespace sacore
