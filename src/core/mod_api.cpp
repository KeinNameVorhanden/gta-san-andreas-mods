#include "mod_api.h"

#include "events.h"
#include "adapters.h"
#include "game/adapter.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <format>

namespace sacore {
namespace {

ModApi* g_api = nullptr;
constexpr UINT kSaveGameMessage = WM_APP + 0x5A3;
uint32_t g_nextShutdownId = 0x80000000u;  // shutdown subscriptions live outside the event bus

// SEH guard without objects that need unwinding in this frame.
bool RunGuarded(const std::function<void()>* call, DWORD* code) {
    __try {
        (*call)();
        return true;
    } __except (*code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

size_t CopyOut(const std::string& text, char* buffer, size_t size) {
    if (buffer && size > 0) {
        const size_t n = std::min(text.size(), size - 1);
        std::memcpy(buffer, text.data(), n);
        buffer[n] = '\0';
    }
    return text.size();
}

SacoreEvent BaseEvent(SacoreEventType type) {
    SacoreEvent e{};
    e.struct_size = sizeof(SacoreEvent);
    e.type = type;
    return e;
}

}  // namespace

ModApi::ModApi(Logger& logger, EventBus& bus, Engine& engine, Game& game, PatchManager& patches,
               const RingBufferSink* recentLog)
    : logger_(logger), bus_(bus), engine_(engine), game_(game), patches_(patches), recentLog_(recentLog) {
    g_api = this;
    // save_game: the game's save routine presents frames itself, so it runs from the message loop.
    engine_.AddMessageFilter("core.modapi", [this](HWND, UINT message, WPARAM, LPARAM, LRESULT& result) {
        if (message == kSaveGameMessage && !pendingLoad_.empty()) {
            const std::string load = pendingLoad_;
            pendingLoad_.clear();
            if (saves_) {
                saves_->LoadFrom(load);
            }
            result = 0;
            return true;
        }
        if (message != kSaveGameMessage || pendingSave_.empty()) {
            return false;
        }
        const std::string name = pendingSave_;
        pendingSave_.clear();
        if (saves_) {
            saves_->SaveTo(name);
        }
        result = 0;
        return true;
    });
}

ModApi::~ModApi() {
    g_api = nullptr;
}

void ModApi::FillHost(ModRecord&, SacoreHost& host) {
    host.capability_state = &CapabilityState;
    host.subscribe = &Subscribe;
    host.unsubscribe = &Unsubscribe;
    host.game_state = &GameState;
    host.get_player = &GetPlayer;
    host.set_money = &SetMoney;
    host.config_bool = &ConfigBool;
    host.config_int = &ConfigInt;
    host.config_double = &ConfigDouble;
    host.config_string = &ConfigString;
    host.d3d_device = &D3dDevice;
    host.game_window = &GameWindow;
    host.add_message_filter = &AddMessageFilter;
    host.install_hook = &InstallHook;
    host.symbol_address = &SymbolAddress;
    host.diagnostics_text = &DiagnosticsTextC;
    host.settings_list = &SettingsList;
    host.settings_set = &SettingsSet;
    host.save_game = &SaveGame;
    host.save_list = &SaveList;
    host.load_game = &LoadGame;
    host.write_memory = &WriteMemoryC;
    host.remove_patch = &RemovePatchC;
    host.core_setting = &CoreSettingC;
    host.import_list = &ImportListC;
    host.import_save = &ImportSaveC;
    host.savedata_set = &SaveDataSet;
    host.savedata_get = &SaveDataGet;
    host.save_slot = &SaveSlotC;
}

void ModApi::Guarded(ModRecord& mod, const char* where, const std::function<void()>& call) {
    if (mod.state != ModState::Running) {
        return;
    }
    DWORD code = 0;
    if (!RunGuarded(&call, &code) && loader_) {
        loader_->MarkFailed(mod, std::format("crashed in {} with exception {:#010x}; mod disabled for this session",
                                             where, code));
    }
}

void ModApi::ReleaseMod(ModRecord& mod) {
    bus_.UnsubscribeOwner(Owner(mod));
    engine_.RemoveMessageFilters(Owner(mod));
}

void ModApi::PublishShutdown() {
    const SacoreEvent event = BaseEvent(SACORE_EVENT_SHUTDOWN);
    if (!loader_) {
        return;
    }
    for (const auto& mod : loader_->Mods()) {
        const auto handlers = shutdownHandlers_.find(mod->id);
        if (handlers == shutdownHandlers_.end()) {
            continue;
        }
        for (const ShutdownHandler& h : handlers->second) {
            if (h.type != SACORE_EVENT_SHUTDOWN) {
                continue;
            }
            Guarded(*mod, "its shutdown event handler", [&] { h.callback(&event, h.user); });
        }
    }
}

SacoreCapabilityState ModApi::CapabilityState(const SacoreHost*, const char* id) {
    if (!g_api || !id) {
        return SACORE_CAPABILITY_NOT_INVESTIGATED;
    }
    switch (g_api->game_.Adapter().State(id)) {
        case CapabilityState::Supported: return SACORE_CAPABILITY_SUPPORTED;
        case CapabilityState::Experimental: return SACORE_CAPABILITY_EXPERIMENTAL;
        case CapabilityState::Unsupported: return SACORE_CAPABILITY_UNSUPPORTED;
        default: return SACORE_CAPABILITY_NOT_INVESTIGATED;
    }
}

uint32_t ModApi::Subscribe(const SacoreHost* host, SacoreEventType type, SacoreEventCallback callback, void* user) {
    if (!g_api || !host || !callback) {
        return 0;
    }
    ModApi& api = *g_api;
    ModRecord& mod = Record(host);
    const std::string owner = Owner(mod);
    ModRecord* record = &mod;
    auto deliver = [&api, record, callback, user](SacoreEvent event) {
        api.Guarded(*record, "an event handler", [&] { callback(&event, user); });
    };
    SubscriptionId id = 0;
    switch (type) {
        case SACORE_EVENT_FRAME:
            id = api.bus_.Subscribe<events::FramePresented>(owner, [deliver](const events::FramePresented& e) {
                SacoreEvent out = BaseEvent(SACORE_EVENT_FRAME);
                out.frame = e.frame;
                out.device = e.device;
                deliver(out);
            });
            break;
        case SACORE_EVENT_GAME_TICK:
            id = api.bus_.Subscribe<events::GameTick>(
                owner, [deliver](const events::GameTick&) { deliver(BaseEvent(SACORE_EVENT_GAME_TICK)); });
            break;
        case SACORE_EVENT_GAME_STATE_CHANGED:
            id = api.bus_.Subscribe<events::GameStateChanged>(owner, [deliver](const events::GameStateChanged& e) {
                SacoreEvent out = BaseEvent(SACORE_EVENT_GAME_STATE_CHANGED);
                out.state_from = e.from;
                out.state_to = e.to;
                deliver(out);
            });
            break;
        case SACORE_EVENT_SAVE_STARTED:
            id = api.bus_.Subscribe<events::SaveStarted>(
                owner, [deliver](const events::SaveStarted&) { deliver(BaseEvent(SACORE_EVENT_SAVE_STARTED)); });
            break;
        case SACORE_EVENT_SAVE_COMPLETED:
            id = api.bus_.Subscribe<events::SaveCompleted>(owner, [deliver](const events::SaveCompleted& e) {
                SacoreEvent out = BaseEvent(SACORE_EVENT_SAVE_COMPLETED);
                out.success = e.success;
                deliver(out);
            });
            break;
        case SACORE_EVENT_LOAD_STARTED:
            id = api.bus_.Subscribe<events::LoadStarted>(
                owner, [deliver](const events::LoadStarted&) { deliver(BaseEvent(SACORE_EVENT_LOAD_STARTED)); });
            break;
        case SACORE_EVENT_LOAD_COMPLETED:
            id = api.bus_.Subscribe<events::LoadCompleted>(owner, [deliver](const events::LoadCompleted& e) {
                SacoreEvent out = BaseEvent(SACORE_EVENT_LOAD_COMPLETED);
                out.success = e.success;
                deliver(out);
            });
            break;
        case SACORE_EVENT_DEVICE_CREATED:
            id = api.bus_.Subscribe<events::DeviceCreated>(owner, [deliver](const events::DeviceCreated& e) {
                SacoreEvent out = BaseEvent(SACORE_EVENT_DEVICE_CREATED);
                out.device = e.device;
                out.window = e.window;
                deliver(out);
            });
            break;
        case SACORE_EVENT_DEVICE_RESET:
            id = api.bus_.Subscribe<events::DeviceReset>(owner, [deliver](const events::DeviceReset& e) {
                SacoreEvent out = BaseEvent(SACORE_EVENT_DEVICE_RESET);
                out.device = e.device;
                out.before_reset = e.beforeReset;
                out.success = e.succeeded;
                deliver(out);
            });
            break;
        case SACORE_EVENT_SHUTDOWN:
        case SACORE_EVENT_CONFIG_CHANGED: {
            // Delivered to this mod only, outside the event bus.
            const uint32_t directId = g_nextShutdownId++;
            api.shutdownHandlers_[mod.id].push_back({directId, callback, user, type});
            return directId;
        }
        default:
            api.logger_.Logf(LogLevel::Error, LogCategory::Mods, mod.id, "subscribe: unknown event type {}",
                             static_cast<int>(type));
            return 0;
    }
    return static_cast<uint32_t>(id);
}

void ModApi::Unsubscribe(const SacoreHost* host, uint32_t subscription) {
    if (!g_api || !host) {
        return;
    }
    if (subscription & 0x80000000u) {
        std::erase_if(g_api->shutdownHandlers_[Record(host).id],
                      [subscription](const ShutdownHandler& h) { return h.id == subscription; });
        return;
    }
    g_api->bus_.Unsubscribe(subscription);
}

int32_t ModApi::GameState(const SacoreHost*) {
    return g_api ? g_api->game_.State() : -1;
}

int32_t ModApi::GetPlayer(const SacoreHost*, SacorePlayer* out) {
    if (!g_api || !out) {
        return 0;
    }
    const auto player = g_api->game_.Player();
    if (!player) {
        return 0;
    }
    out->struct_size = sizeof(SacorePlayer);
    out->health = player->health;
    out->armour = player->armour;
    out->money = player->money;
    out->wanted_level = player->wantedLevel;
    std::memcpy(out->position, player->position, sizeof(out->position));
    out->area = player->pedArea;
    return 1;
}

int32_t ModApi::SetMoney(const SacoreHost*, int32_t money) {
    return g_api && g_api->game_.SetMoney(money) ? 1 : 0;
}

namespace {
const SettingValue* FindSetting(const ModRecord& mod, const char* key) {
    if (!key) {
        return nullptr;
    }
    const auto& values = mod.config.Values();
    const auto found = values.find(std::string_view(key));
    return found == values.end() ? nullptr : &found->second;
}
}  // namespace

int32_t ModApi::ConfigBool(const SacoreHost* host, const char* key, int32_t fallback) {
    const SettingValue* value = host ? FindSetting(Record(host), key) : nullptr;
    if (const auto* b = value ? std::get_if<bool>(value) : nullptr) {
        return *b ? 1 : 0;
    }
    if (g_api && host) {
        g_api->logger_.Logf(LogLevel::Error, LogCategory::Configuration, Record(host).id,
                            "config_bool('{}'): not declared as a bool in mod.toml [config]", key ? key : "");
    }
    return fallback;
}

int64_t ModApi::ConfigInt(const SacoreHost* host, const char* key, int64_t fallback) {
    const SettingValue* value = host ? FindSetting(Record(host), key) : nullptr;
    if (const auto* i = value ? std::get_if<int64_t>(value) : nullptr) {
        return *i;
    }
    if (g_api && host) {
        g_api->logger_.Logf(LogLevel::Error, LogCategory::Configuration, Record(host).id,
                            "config_int('{}'): not declared as an integer in mod.toml [config]", key ? key : "");
    }
    return fallback;
}

double ModApi::ConfigDouble(const SacoreHost* host, const char* key, double fallback) {
    const SettingValue* value = host ? FindSetting(Record(host), key) : nullptr;
    if (const auto* d = value ? std::get_if<double>(value) : nullptr) {
        return *d;
    }
    if (g_api && host) {
        g_api->logger_.Logf(LogLevel::Error, LogCategory::Configuration, Record(host).id,
                            "config_double('{}'): not declared as a number in mod.toml [config]", key ? key : "");
    }
    return fallback;
}

size_t ModApi::ConfigString(const SacoreHost* host, const char* key, char* buffer, size_t size) {
    const SettingValue* value = host ? FindSetting(Record(host), key) : nullptr;
    if (const auto* s = value ? std::get_if<std::string>(value) : nullptr) {
        return CopyOut(*s, buffer, size);
    }
    if (g_api && host) {
        g_api->logger_.Logf(LogLevel::Error, LogCategory::Configuration, Record(host).id,
                            "config_string('{}'): not declared as a string in mod.toml [config]", key ? key : "");
    }
    return CopyOut("", buffer, size);
}

void* ModApi::D3dDevice(const SacoreHost*) {
    return g_api ? g_api->engine_.Device() : nullptr;
}

void* ModApi::GameWindow(const SacoreHost*) {
    return g_api ? g_api->engine_.GameWindow() : nullptr;
}

int32_t ModApi::AddMessageFilter(const SacoreHost* host, SacoreMessageFilter filter, void* user) {
    if (!g_api || !host || !filter) {
        return 0;
    }
    ModApi& api = *g_api;
    ModRecord* record = &Record(host);
    api.engine_.AddMessageFilter(Owner(*record), [&api, record, filter, user](HWND window, UINT message, WPARAM wParam,
                                                                              LPARAM lParam, LRESULT& result) {
        int consumed = 0;
        intptr_t value = 0;
        api.Guarded(*record, "a window message filter", [&] {
            consumed = filter(window, message, wParam, lParam, &value, user);
        });
        result = value;
        return consumed != 0;
    });
    return 1;
}

int32_t ModApi::InstallHook(const SacoreHost* host, const SacorePatchInfo* info, void* target, void* detour,
                            void** original) {
    if (!g_api || !host || !info || !info->id || !original) {
        return 0;
    }
    const ModRecord& mod = Record(host);
    PatchInfo patch{mod.id + "." + info->id, mod.id, info->target ? info->target : "?",
                    info->reason ? info->reason : "", info->expected ? info->expected : "",
                    info->on_failure ? info->on_failure : ""};
    return g_api->patches_.InstallHook(patch, target, detour, original) ? 1 : 0;
}

int32_t ModApi::WriteMemoryC(const SacoreHost* host, const SacorePatchInfo* info, void* target, const void* bytes,
                             size_t size) {
    if (!g_api || !host || !info || !info->id || !target || !bytes || size == 0) {
        return 0;
    }
    const ModRecord& mod = Record(host);
    PatchInfo patch{mod.id + "." + info->id, mod.id, info->target ? info->target : "?",
                    info->reason ? info->reason : "", info->expected ? info->expected : "",
                    info->on_failure ? info->on_failure : ""};
    // Writing again over an installed patch would record the patched bytes as the original.
    if (const PatchRecord* existing = g_api->patches_.Find(patch.id); existing && existing->state == PatchState::Installed) {
        return 1;
    }
    return g_api->patches_.WriteMemory(patch, target, bytes, size) ? 1 : 0;
}

int32_t ModApi::RemovePatchC(const SacoreHost* host, const char* id) {
    if (!g_api || !host || !id) {
        return 0;
    }
    return g_api->patches_.Remove(Record(host).id + "." + id) ? 1 : 0;
}

size_t ModApi::CoreSettingC(const SacoreHost*, const char* key, char* buffer, size_t size) {
    if (!g_api || !key) {
        return static_cast<size_t>(-1);
    }
    const auto found = g_api->coreSettings_.find(key);
    return found == g_api->coreSettings_.end() ? static_cast<size_t>(-1) : CopyOut(found->second, buffer, size);
}

uintptr_t ModApi::SymbolAddress(const SacoreHost*, const char* symbol) {
    return g_api && symbol ? g_api->game_.Adapter().Address(symbol) : 0;
}

size_t ModApi::DiagnosticsTextC(const SacoreHost*, const char* kind, char* buffer, size_t size) {
    return CopyOut(g_api && kind ? g_api->DiagnosticsText(kind) : std::string{}, buffer, size);
}

int32_t ModApi::SaveDataSet(const SacoreHost* host, const char* key, const char* value) {
    if (!g_api || !g_api->saves_ || !host || !key || !value) {
        return 0;
    }
    g_api->saves_->SetModValue(Record(host).id, key, value);
    return 1;
}

size_t ModApi::SaveDataGet(const SacoreHost* host, const char* key, char* buffer, size_t size) {
    if (!g_api || !g_api->saves_ || !host || !key) {
        return static_cast<size_t>(-1);
    }
    const auto value = g_api->saves_->GetModValue(Record(host).id, key);
    return value ? CopyOut(*value, buffer, size) : static_cast<size_t>(-1);
}

int32_t ModApi::SaveSlotC(const SacoreHost*) {
    return g_api && g_api->saves_ ? g_api->saves_->CurrentSlot() : 0;
}

namespace {
std::string TypeOf(const SettingValue& value) {
    return std::holds_alternative<bool>(value)      ? "bool"
           : std::holds_alternative<int64_t>(value) ? "int"
           : std::holds_alternative<double>(value)  ? "double"
                                                    : "string";
}

std::string TextOf(const SettingValue& value) {
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b ? "true" : "false";
    }
    if (const auto* i = std::get_if<int64_t>(&value)) {
        return std::to_string(*i);
    }
    if (const auto* d = std::get_if<double>(&value)) {
        return std::format("{}", *d);
    }
    return std::get<std::string>(value);
}

std::string NoTabs(std::string text) {
    std::replace(text.begin(), text.end(), '\t', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}
}  // namespace

int32_t ModApi::SaveGame(const SacoreHost* host, const char* name) {
    if (!g_api || !g_api->saves_ || !name) {
        return 0;
    }
    ModApi& api = *g_api;
    const std::string save(name);
    const char* refusal = nullptr;
    if (api.saves_->FileFor(save).empty()) {
        refusal = "unknown save name (slot1..slot8, quick1..quick9, auto1..auto9, story1..story120)";
    } else if (!api.game_.Adapter().Available(save.starts_with("slot") ? adapters::kCapSaveWriteSlot
                                                                       : adapters::kCapSaveExtraSlots)) {
        refusal = "the save routine is unavailable on this build";
    } else if (api.game_.State() != 9) {
        refusal = "not playing";
    } else if (api.game_.CurrentArea() != 0) {
        refusal = "inside an interior or cutscene";
    } else if (!api.pendingSave_.empty()) {
        refusal = "another save is pending";
    } else if (api.game_.OnMissionOrMiniGame()) {
        refusal = "a mission or mini game runs (such saves reload broken or crash while loading)";
    }
    if (refusal) {
        api.logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, host ? Record(host).id : "?",
                         "save_game({}) refused: {}", save, refusal);
        return 0;
    }
    api.pendingSave_ = save;
    PostMessageW(static_cast<HWND>(api.engine_.GameWindow()), kSaveGameMessage, 0, 0);
    return 1;
}

int32_t ModApi::LoadGame(const SacoreHost* host, const char* name) {
    if (!g_api || !g_api->saves_ || !name || g_api->saves_->FileFor(name).empty()) {
        return 0;
    }
    ModApi& api = *g_api;
    const std::filesystem::path file = api.saves_->FileFor(name);
    std::error_code error;
    if (!std::filesystem::exists(file, error) || !api.game_.Adapter().Available("save.load_slot") ||
        !api.pendingLoad_.empty()) {
        api.logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, host ? Record(host).id : "?",
                         "load_game({}) refused: file missing, loading unavailable or a load is pending", name);
        return 0;
    }
    api.pendingLoad_ = name;
    PostMessageW(static_cast<HWND>(api.engine_.GameWindow()), kSaveGameMessage, 0, 0);
    return 1;
}

size_t ModApi::SaveList(const SacoreHost*, char* buffer, size_t size) {
    std::string text;
    if (g_api && g_api->saves_) {
        for (const auto& entry : g_api->saves_->ListSaves()) {
            const std::u8string path = entry.file.u8string();
            text += std::format("{}\t{}\t{}\t{}\n", entry.name, std::string(path.begin(), path.end()), entry.modified,
                                entry.valid ? 1 : 0);
        }
    }
    return CopyOut(text, buffer, size);
}

size_t ModApi::ImportListC(const SacoreHost*, char* buffer, size_t size) {
    std::string text;
    if (g_api && g_api->saves_) {
        for (const auto& entry : g_api->saves_->ListImports()) {
            text += std::format("{}\t{}\t{}\t{}\t{:08x}\n", entry.file, entry.size, entry.valid ? 1 : 0, entry.problem,
                                entry.versionId);
        }
    }
    return CopyOut(text, buffer, size);
}

int32_t ModApi::ImportSaveC(const SacoreHost* host, const char* file, const char* name) {
    if (!g_api || !g_api->saves_ || !host || !file || !name) {
        return 0;
    }
    const std::string problem = g_api->saves_->Import(file, name);
    if (!problem.empty()) {
        g_api->logger_.Logf(LogLevel::Warn, LogCategory::GameAdapter, Record(host).id, "import of {} as {} refused: {}",
                            file, name, problem);
        return 0;
    }
    return 1;
}

size_t ModApi::SettingsList(const SacoreHost*, char* buffer, size_t size) {
    std::string text;
    if (g_api && g_api->loader_) {
        for (const auto& mod : g_api->loader_->Mods()) {
            if (mod->state != ModState::Running || !mod->manifest) {
                continue;
            }
            for (const SettingSpec& spec : mod->manifest->configSchema) {
                const auto current = mod->config.Values().find(spec.key);
                std::string allowed;
                for (const std::string& option : spec.allowedStrings) {
                    allowed += (allowed.empty() ? "" : "|") + option;
                }
                text += std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", mod->id, spec.key, TypeOf(spec.defaultValue),
                                    NoTabs(TextOf(current != mod->config.Values().end() ? current->second
                                                                                       : spec.defaultValue)),
                                    spec.minimum ? std::format("{}", *spec.minimum) : "",
                                    spec.maximum ? std::format("{}", *spec.maximum) : "", NoTabs(allowed),
                                    NoTabs(spec.description));
            }
        }
    }
    return CopyOut(text, buffer, size);
}

int32_t ModApi::SettingsSet(const SacoreHost* host, const char* modId, const char* key, const char* value) {
    if (!g_api || !g_api->loader_ || !modId || !key || !value) {
        return 0;
    }
    ModApi& api = *g_api;
    const std::string caller = host ? Record(host).id : std::string("?");
    for (const auto& mod : api.loader_->Mods()) {
        if (mod->id != modId || mod->state != ModState::Running || !mod->manifest) {
            continue;
        }
        const auto& schema = mod->manifest->configSchema;
        const auto spec = std::find_if(schema.begin(), schema.end(), [&](const SettingSpec& s) { return s.key == key; });
        if (spec == schema.end()) {
            api.logger_.Logf(LogLevel::Error, LogCategory::Configuration, caller,
                             "settings_set: {} has no setting '{}'", modId, key);
            return 0;
        }
        std::string problem;
        const auto parsed = ConfigLoader::ParseValue(*spec, value, problem);
        if (!parsed) {
            api.logger_.Logf(LogLevel::Warn, LogCategory::Configuration, caller, "settings_set {}: {}", modId, problem);
            return 0;
        }
        mod->config.Set(spec->key, *parsed);
        if (!mod->configFile.empty()) {
            ConfigSchema full;
            full.settings = schema;
            std::ofstream(mod->configFile, std::ios::binary | std::ios::trunc)
                << ConfigLoader::Render(full, mod->config, {});
        }
        api.logger_.Logf(LogLevel::Info, LogCategory::Configuration, mod->id, "setting {} = {} (changed by {})", key,
                         TextOf(*parsed), caller);
        SacoreEvent event = BaseEvent(SACORE_EVENT_CONFIG_CHANGED);
        event.text = spec->key.c_str();
        const auto handlers = api.shutdownHandlers_.find(mod->id);
        if (handlers != api.shutdownHandlers_.end()) {
            const auto copy = handlers->second;  // a handler may unsubscribe
            for (const ShutdownHandler& h : copy) {
                if (h.type == SACORE_EVENT_CONFIG_CHANGED) {
                    api.Guarded(*mod, "its settings handler", [&] { h.callback(&event, h.user); });
                }
            }
        }
        return 1;
    }
    api.logger_.Logf(LogLevel::Error, LogCategory::Configuration, caller, "settings_set: no running mod '{}'", modId);
    return 0;
}

std::string ModApi::DiagnosticsText(std::string_view kind) const {
    std::string text;
    if (kind == "mods" && loader_) {
        for (const auto& mod : loader_->Mods()) {
            text += std::format("{} {} {}{}\n", mod->id,
                                mod->manifest ? mod->manifest->version.ToString() : std::string("?"), ToString(mod->state),
                                mod->reason.empty() ? "" : " (" + mod->reason + ")");
        }
    } else if (kind == "capabilities") {
        for (const CapabilityStatus& c : game_.Adapter().Capabilities()) {
            text += std::format("{} {}\n", c.id, sacore::ToString(c.state));
        }
    } else if (kind == "patches") {
        text = patches_.StatusReport();
    } else if (kind == "log" && recentLog_) {
        for (const std::string& line : recentLog_->Lines()) {
            text += line + "\n";
        }
    }
    return text;
}

}  // namespace sacore
