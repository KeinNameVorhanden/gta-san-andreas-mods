// Header-only C++20 convenience layer over the SACore C ABI (sacore_api.h). Compile it into your mod; it adds no
// ABI of its own, so mods built with any compiler settings keep working with any core of API version 1.
#pragma once

#include <sacore/sacore_api.h>

#include <cstddef>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacore::mod {

class Host {
public:
    explicit Host(const SacoreHost* host) : host_(host) {}

    const SacoreHost* Raw() const { return host_; }
    std::string_view Id() const { return host_->mod_id; }
    std::string_view GameBuild() const { return host_->game_build; }
    std::string_view CoreVersion() const { return host_->core_version; }

    void Log(SacoreLogLevel level, std::string_view message) const {
        const std::string text(message);
        host_->log(host_, level, text.c_str());
    }
    template <class... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args) const {
        Log(SACORE_LOG_INFO, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args) const {
        Log(SACORE_LOG_WARN, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args) const {
        Log(SACORE_LOG_ERROR, std::format(fmt, std::forward<Args>(args)...));
    }

    SacoreCapabilityState Capability(const char* id) const { return host_->capability_state(host_, id); }
    // True for supported and experimental capabilities.
    bool Has(const char* id) const {
        const auto state = Capability(id);
        return state == SACORE_CAPABILITY_SUPPORTED || state == SACORE_CAPABILITY_EXPERIMENTAL;
    }

    // Subscribes a callable; it lives as long as this Host object (keep the Host static in your mod).
    uint32_t On(SacoreEventType type, std::function<void(const SacoreEvent&)> handler) {
        handlers_.push_back(std::make_unique<std::function<void(const SacoreEvent&)>>(std::move(handler)));
        return host_->subscribe(host_, type, &Thunk, handlers_.back().get());
    }
    void Off(uint32_t subscription) { host_->unsubscribe(host_, subscription); }

    int GameState() const { return host_->game_state(host_); }
    std::optional<SacorePlayer> Player() const {
        SacorePlayer player{};
        player.struct_size = sizeof(player);
        return host_->get_player(host_, &player) ? std::optional(player) : std::nullopt;
    }
    bool SetMoney(int money) const { return host_->set_money(host_, money) != 0; }

    bool ConfigBool(const char* key, bool fallback = false) const { return host_->config_bool(host_, key, fallback) != 0; }
    int64_t ConfigInt(const char* key, int64_t fallback = 0) const { return host_->config_int(host_, key, fallback); }
    double ConfigDouble(const char* key, double fallback = 0) const { return host_->config_double(host_, key, fallback); }
    std::string ConfigString(const char* key) const {
        const size_t length = host_->config_string(host_, key, nullptr, 0);
        std::string text(length, '\0');
        host_->config_string(host_, key, text.data(), length + 1);
        return text;
    }

    // Hooks `target` through the core patch manager (registered, switchable in core.toml, removed at shutdown).
    template <class Fn>
    bool Hook(const char* id, const char* target, const char* reason, Fn* function, Fn* detour, Fn** original) const {
        SacorePatchInfo info{sizeof(SacorePatchInfo), id, target, reason, "", ""};
        return host_->install_hook(host_, &info, reinterpret_cast<void*>(function), reinterpret_cast<void*>(detour),
                                   reinterpret_cast<void**>(original)) != 0;
    }

    std::string Diagnostics(const char* kind) const {
        const size_t length = host_->diagnostics_text(host_, kind, nullptr, 0);
        std::string text(length, '\0');
        host_->diagnostics_text(host_, kind, text.data(), length + 1);
        return text;
    }

    // Save side-car data; requires core 0.8.0 (struct_size check).
    bool HasSaveData() const { return host_->struct_size >= offsetof(SacoreHost, save_slot) + sizeof(void*); }
    bool SetSaveValue(const char* key, const std::string& value) const {
        return HasSaveData() && host_->savedata_set(host_, key, value.c_str()) != 0;
    }
    std::optional<std::string> SaveValue(const char* key) const {
        if (!HasSaveData()) {
            return std::nullopt;
        }
        const size_t length = host_->savedata_get(host_, key, nullptr, 0);
        if (length == static_cast<size_t>(-1)) {
            return std::nullopt;
        }
        std::string text(length, '\0');
        host_->savedata_get(host_, key, text.data(), length + 1);
        return text;
    }
    int SaveSlot() const { return HasSaveData() ? host_->save_slot(host_) : 0; }

    // Live settings of all running mods; requires core 1.0.0 (struct_size check). Lines with tab-separated fields:
    // mod id, key, type, value, min, max, allowed (|-separated), description.
    bool HasLiveSettings() const { return host_->struct_size >= offsetof(SacoreHost, settings_set) + sizeof(void*); }
    std::string SettingsList() const {
        if (!HasLiveSettings()) {
            return {};
        }
        std::string text(host_->settings_list(host_, nullptr, 0), '\0');
        host_->settings_list(host_, text.data(), text.size() + 1);
        return text;
    }
    bool SetSetting(const std::string& modId, const std::string& key, const std::string& value) {
        return HasLiveSettings() && host_->settings_set(host_, modId.c_str(), key.c_str(), value.c_str()) != 0;
    }

    // Saves to "slot1".."slot8", "quick1".."quick9", "auto1".."auto9" or (core 1.3.0) "story1".."story120";
    // requires core 1.0.0.
    // Result: SACORE_EVENT_SAVE_COMPLETED. SaveList(): lines "name \t file \t unix time \t valid".
    bool HasSaveGame() const { return host_->struct_size >= offsetof(SacoreHost, save_list) + sizeof(void*); }
    bool SaveGame(const std::string& name) const { return HasSaveGame() && host_->save_game(host_, name.c_str()) != 0; }
    bool HasLoadGame() const { return host_->struct_size >= offsetof(SacoreHost, load_game) + sizeof(void*); }
    bool LoadGame(const std::string& name) const { return HasLoadGame() && host_->load_game(host_, name.c_str()) != 0; }

    // Memory patches and core settings; require core 1.2.0 (struct_size check).
    bool HasPatching() const { return host_->struct_size >= offsetof(SacoreHost, core_setting) + sizeof(void*); }
    // Writes bytes through the core patch manager; restored by RemovePatch(id) or at shutdown.
    bool WriteMemory(const char* id, const char* target, const char* reason, uintptr_t address, const void* bytes,
                     size_t size) const {
        SacorePatchInfo info{sizeof(SacorePatchInfo), id, target, reason, "", ""};
        return HasPatching() && address &&
               host_->write_memory(host_, &info, reinterpret_cast<void*>(address), bytes, size) != 0;
    }
    bool RemovePatch(const char* id) const { return HasPatching() && host_->remove_patch(host_, id) != 0; }
    std::optional<std::string> CoreSetting(const char* key) const {
        if (!HasPatching()) {
            return std::nullopt;
        }
        const size_t length = host_->core_setting(host_, key, nullptr, 0);
        if (length == static_cast<size_t>(-1)) {
            return std::nullopt;
        }
        std::string text(length, '\0');
        host_->core_setting(host_, key, text.data(), length + 1);
        return text;
    }
    std::string SaveList() const {
        if (!HasSaveGame()) {
            return {};
        }
        std::string text(host_->save_list(host_, nullptr, 0), '\0');
        host_->save_list(host_, text.data(), text.size() + 1);
        return text;
    }
    struct SaveEntry {
        std::string name;  // "slot3", "quick1", "auto2"
        std::string file;
        long long modified = 0;  // Unix seconds
        bool valid = false;
    };
    // SaveList() parsed.
    std::vector<SaveEntry> Saves() const {
        std::vector<SaveEntry> saves;
        const std::string text = SaveList();
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                end = text.size();
            }
            const std::string_view line(text.data() + start, end - start);
            start = end + 1;
            SaveEntry entry;
            size_t field = 0, from = 0;
            for (size_t i = 0; i <= line.size(); ++i) {
                if (i == line.size() || line[i] == '\t') {
                    const std::string value(line.substr(from, i - from));
                    if (field == 0) entry.name = value;
                    if (field == 1) entry.file = value;
                    if (field == 2) entry.modified = std::atoll(value.c_str());
                    if (field == 3) entry.valid = value == "1";
                    ++field;
                    from = i + 1;
                }
            }
            if (!entry.name.empty()) {
                saves.push_back(std::move(entry));
            }
        }
        return saves;
    }
    // Next name in a rotation "<prefix>1".."<prefix><count>": the one after the newest existing save.
    std::string NextRotatingSave(const std::string& prefix, int count) const {
        int newest = 0;
        long long newestTime = -1;
        for (const SaveEntry& save : Saves()) {
            if (save.name.size() == prefix.size() + 1 && save.name.starts_with(prefix) && save.modified >= newestTime) {
                newestTime = save.modified;
                newest = save.name.back() - '0';
            }
        }
        return prefix + std::to_string(newest % (count < 1 ? 1 : count) + 1);
    }

    // Community save import; requires core 1.2.20 (struct_size check).
    bool HasImport() const { return host_->struct_size >= offsetof(SacoreHost, import_save) + sizeof(void*); }
    std::string ImportList() const {
        if (!HasImport()) {
            return {};
        }
        std::string text(host_->import_list(host_, nullptr, 0), '\0');
        host_->import_list(host_, text.data(), text.size() + 1);
        return text;
    }
    bool ImportSave(const std::string& file, const std::string& name) const {
        return HasImport() && host_->import_save(host_, file.c_str(), name.c_str()) != 0;
    }

    // Calls `callback` when `virtualKey` (VK_* code) is pressed in the game window; key repeat is ignored and the
    // key still reaches the game. Rebinding: read the key code from your [config] and call OnKey again.
    void OnKey(unsigned virtualKey, std::function<void()> callback) {
        if (keys_.empty()) {
            host_->add_message_filter(host_, &KeyThunk, this);
        }
        keys_.push_back({virtualKey, std::move(callback)});
    }
    void ClearKeys() {
        for (auto& key : keys_) {
            key.code = 0;
        }
    }

    // BUILD-SPECIFIC: address of an adapter symbol (docs/ADDRESSES.md), 0 if unknown on this build.
    uintptr_t Symbol(const char* name) const { return host_->symbol_address(host_, name); }
    // A game function as a typed pointer, e.g. GameFunction<void(__cdecl*)(int)>("CClock::SetGameClock").
    template <class Fn>
    Fn GameFunction(const char* name) const {
        return reinterpret_cast<Fn>(Symbol(name));
    }
    // A game variable, e.g. GameVariable<float>("CTimer::ms_fTimeScale"); nullptr if unknown.
    template <class T>
    T* GameVariable(const char* name) const {
        return reinterpret_cast<T*>(Symbol(name));
    }

private:
    struct KeyBinding {
        unsigned code;
        std::function<void()> callback;
    };

    static void Thunk(const SacoreEvent* event, void* user) {
        (*static_cast<std::function<void(const SacoreEvent&)>*>(user))(*event);
    }
    static int KeyThunk(void*, uint32_t message, uintptr_t wParam, intptr_t lParam, intptr_t*, void* user) {
        constexpr uint32_t kKeyDown = 0x0100, kSysKeyDown = 0x0104;
        const bool repeat = (lParam & (1 << 30)) != 0;
        if ((message == kKeyDown || message == kSysKeyDown) && !repeat) {
            for (auto& key : static_cast<Host*>(user)->keys_) {
                if (key.code != 0 && key.code == wParam) {
                    key.callback();
                }
            }
        }
        return 0;
    }

    const SacoreHost* host_;
    std::vector<std::unique_ptr<std::function<void(const SacoreEvent&)>>> handlers_;
    std::vector<KeyBinding> keys_;
};

}  // namespace sacore::mod
