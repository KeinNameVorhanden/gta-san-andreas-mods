// Implements the C mod API (sacore_api.h) on top of the core services. One instance per process.
#pragma once

#include "engine/engine.h"
#include "event_bus.h"
#include "game/game.h"
#include "logger.h"
#include "mod_loader.h"
#include "patch_manager.h"
#include "saves/save_service.h"

#include <sacore/sacore_api.h>

#include <functional>
#include <map>
#include <string>

namespace sacore {

class ModApi {
public:
    ModApi(Logger& logger, EventBus& bus, Engine& engine, Game& game, PatchManager& patches,
           const RingBufferSink* recentLog);
    ~ModApi();
    ModApi(const ModApi&) = delete;
    ModApi& operator=(const ModApi&) = delete;

    void SetLoader(ModLoader* loader) { loader_ = loader; }
    // core.toml values for core_setting (text form).
    void SetCoreSettings(std::map<std::string, std::string> settings) { coreSettings_ = std::move(settings); }
    void SetSaves(SaveService* saves) { saves_ = saves; }
    void FillHost(ModRecord& mod, SacoreHost& host);
    // Drops every subscription and message filter of a mod (failure or shutdown).
    void ReleaseMod(ModRecord& mod);
    // Delivers SACORE_EVENT_SHUTDOWN to all subscribed mods (before their shutdown callbacks).
    void PublishShutdown();
    std::string DiagnosticsText(std::string_view kind) const;
    // Same path as settings_set (validation, file, CONFIG_CHANGED), for core callers such as the test harness.
    static bool SetModSetting(const std::string& mod, const std::string& key, const std::string& value) {
        return SettingsSet(nullptr, mod.c_str(), key.c_str(), value.c_str()) != 0;
    }

private:
    static std::string Owner(const ModRecord& mod) { return "mod:" + mod.id; }
    static ModRecord& Record(const SacoreHost* host) { return *static_cast<ModRecord*>(host->core_internal); }
    // Runs a mod callback under an SEH guard; on a crash the mod is marked failed and released.
    void Guarded(ModRecord& mod, const char* where, const std::function<void()>& call);

    // C entry points (sacore_api.h).
    static SacoreCapabilityState CapabilityState(const SacoreHost* host, const char* id);
    static uint32_t Subscribe(const SacoreHost* host, SacoreEventType type, SacoreEventCallback callback, void* user);
    static void Unsubscribe(const SacoreHost* host, uint32_t subscription);
    static int32_t GameState(const SacoreHost* host);
    static int32_t GetPlayer(const SacoreHost* host, SacorePlayer* out);
    static int32_t SetMoney(const SacoreHost* host, int32_t money);
    static int32_t ConfigBool(const SacoreHost* host, const char* key, int32_t fallback);
    static int64_t ConfigInt(const SacoreHost* host, const char* key, int64_t fallback);
    static double ConfigDouble(const SacoreHost* host, const char* key, double fallback);
    static size_t ConfigString(const SacoreHost* host, const char* key, char* buffer, size_t size);
    static void* D3dDevice(const SacoreHost* host);
    static void* GameWindow(const SacoreHost* host);
    static int32_t AddMessageFilter(const SacoreHost* host, SacoreMessageFilter filter, void* user);
    static int32_t InstallHook(const SacoreHost* host, const SacorePatchInfo* info, void* target, void* detour,
                               void** original);
    static uintptr_t SymbolAddress(const SacoreHost* host, const char* symbol);
    static size_t DiagnosticsTextC(const SacoreHost* host, const char* kind, char* buffer, size_t size);
    static int32_t SaveDataSet(const SacoreHost* host, const char* key, const char* value);
    static size_t SaveDataGet(const SacoreHost* host, const char* key, char* buffer, size_t size);
    static int32_t SaveSlotC(const SacoreHost* host);
    static size_t SettingsList(const SacoreHost* host, char* buffer, size_t size);
    static int32_t SaveGame(const SacoreHost* host, const char* name);
    static size_t SaveList(const SacoreHost* host, char* buffer, size_t size);
    static int32_t LoadGame(const SacoreHost* host, const char* name);
    static int32_t WriteMemoryC(const SacoreHost* host, const SacorePatchInfo* info, void* target, const void* bytes,
                                size_t size);
    static int32_t RemovePatchC(const SacoreHost* host, const char* id);
    static size_t CoreSettingC(const SacoreHost* host, const char* key, char* buffer, size_t size);
    static size_t ImportListC(const SacoreHost* host, char* buffer, size_t size);
    static int32_t ImportSaveC(const SacoreHost* host, const char* file, const char* name);
    static int32_t SettingsSet(const SacoreHost* host, const char* modId, const char* key, const char* value);

    std::map<std::string, std::string> coreSettings_;
    Logger& logger_;
    EventBus& bus_;
    Engine& engine_;
    Game& game_;
    PatchManager& patches_;
    const RingBufferSink* recentLog_;
    ModLoader* loader_ = nullptr;
    SaveService* saves_ = nullptr;
    std::string pendingSave_;
    std::string pendingLoad_;
    struct ShutdownHandler {
        uint32_t id;
        SacoreEventCallback callback;
        void* user;
        SacoreEventType type = SACORE_EVENT_SHUTDOWN;  // also SACORE_EVENT_CONFIG_CHANGED (delivered to one mod)
    };
    std::map<std::string, std::vector<ShutdownHandler>> shutdownHandlers_;
};

}  // namespace sacore
