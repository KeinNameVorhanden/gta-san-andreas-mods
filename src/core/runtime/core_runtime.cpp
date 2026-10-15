#include "core_runtime.h"

#include "adapters.h"
#include "build_info.h"
#include "early_hooks.h"
#include "events.h"
#include "version.h"

#include <sacore/sacore_api.h>

#include <format>
#include <map>
#include <type_traits>
#include <variant>
#include <fstream>
#include <set>
#include <sstream>

#ifndef SACORE_VERSION_STRING
#error "SACORE_VERSION_STRING must be defined by the build"
#endif

namespace sacore {
namespace {

constexpr int kMaxSlowHandlerReports = 50;

std::filesystem::path ModulePath(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::set<std::string> SplitList(const std::string& text) {
    std::set<std::string> items;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const size_t first = item.find_first_not_of(" \t");
        const size_t last = item.find_last_not_of(" \t");
        if (first != std::string::npos) {
            items.insert(item.substr(first, last - first + 1));
        }
    }
    return items;
}

}  // namespace

ConfigSchema CoreRuntime::Schema() {
    ConfigSchema schema;
    schema.version = 2;
    schema.settings = {
        SettingSpec{"log.level", std::string("info"), "Minimum log level: trace, debug, info, warn, error", {}, {},
                    {"trace", "debug", "info", "warn", "error"}},
        SettingSpec{"mods.enabled", true, "Load mods from sacore\\mods", {}, {}, {}},
        SettingSpec{"window.windowed", true,
                    "Run in a window instead of exclusive fullscreen (fullscreen changes the display mode of the "
                    "primary monitor)",
                    {}, {}, {}},
        SettingSpec{"window.client_width", int64_t{0}, "Window client width in pixels (height follows the game's "
                    "aspect ratio); 0 = exactly the resolution chosen in the game's display settings", 0.0, 7680.0,
                    {}},
        SettingSpec{"window.run_in_background", true, "Keep the game running when its window loses focus", {}, {}, {}},
        SettingSpec{"window.focus_on_start", true, "Bring the game window to the front when it starts (false: start "
                    "behind other windows and never take focus by itself)", {}, {}, {}},
        SettingSpec{"audio.start_muted", false, "Mute the game at start (unmute in the Windows volume mixer)", {}, {},
                    {}},
        SettingSpec{"game.skip_intro_movies", true, "Skip the logo and intro movies", {}, {}, {}},
        SettingSpec{"patches.disabled", std::string(""), "Comma-separated patch ids to switch off (see the log or "
                    "docs/ADDRESSES.md)", {}, {}, {}},
        SettingSpec{"stability.crash_dumps", true, "Write a minidump and report to sacore\\crashes when the game "
                    "crashes", {}, {}, {}},
        SettingSpec{"stability.skip_device_selection", true, "Confirm the multi-monitor Device Selection dialog "
                    "automatically with its default device", {}, {}, {}},
        SettingSpec{"saves.separate_user_files", false, "Keep this install's saves and settings in "
                    "sacore\\userdata instead of the shared Documents folder (recommended when several game "
                    "versions are installed)", {}, {}, {}},
        SettingSpec{"saves.backups_per_slot", int64_t{10}, "Backups kept per save slot in sacore\\save-backups",
                    1.0, 100.0, {}},
        SettingSpec{"testing.crash_after_seconds", int64_t{0}, "TEST ONLY: crash deliberately this many seconds "
                    "after the first frame (0 = off)", 0.0, 3600.0, {}},
        SettingSpec{"testing.auto_start_new_game", false, "TEST ONLY: start a new game 3 s after the main menu appears",
                    {}, {}, {}},
        SettingSpec{"testing.give_money_on_start", int64_t{0}, "TEST ONLY: add this much money when gameplay starts",
                    0.0, 1000000.0, {}},
        SettingSpec{"testing.log_state_changes", false, "Log player state changes (health, money, wanted, area)", {},
                    {}, {}},
    };
    // v1 -> v2 (0.9.3): the window follows the game resolution by default. A width equal to the old default (1280)
    // was the written default, not a choice, so it becomes "follow the game".
    schema.migrations.push_back({1, [](SettingMap& values) {
        const auto width = values.find("window.client_width");
        if (width != values.end() && std::holds_alternative<int64_t>(width->second) &&
            std::get<int64_t>(width->second) == 1280) {
            width->second = int64_t{0};
        }
    }});
    return schema;
}

CoreRuntime::CoreRuntime(HMODULE self) : self_(self) {}

void CoreRuntime::Initialize() {
    gameDirectory_ = ModulePath(nullptr).parent_path();
    dataDirectory_ = gameDirectory_ / L"sacore";
    logFile_ = RotateLogFiles(dataDirectory_ / L"logs", "sacore", 5);
    startTick_ = GetTickCount64();
    coreSha256_ = Sha256OfFile(ModulePath(self_));
    auto ring = std::make_unique<RingBufferSink>(std::make_unique<FileLogSink>(logFile_), 200);
    recentLog_ = ring.get();
    logger_ = std::make_unique<Logger>(std::move(ring), LogLevel::Info);
    logger_->Logf(LogLevel::Info, LogCategory::Core, "", "SACore {} (mod API {}) loaded from {}", SACORE_VERSION_STRING,
                  SACORE_API_VERSION, Utf8(ModulePath(self_)));

    const ConfigLoadResult loaded = ConfigLoader::Load(dataDirectory_ / L"config" / L"core.toml", Schema());
    for (const std::string& problem : loaded.problems) {
        logger_->Log(LogLevel::Warn, LogCategory::Configuration, "core.toml", problem);
    }
    if (loaded.backup) {
        logger_->Logf(LogLevel::Info, LogCategory::Configuration, "core.toml", "previous file kept as {}",
                      Utf8(*loaded.backup));
    }
    const Config& config = loaded.config;
    LogLevel level = LogLevel::Info;
    ParseLogLevel(config.GetString("log.level"), level);
    logger_->SetMinimum(level);

    bus_ = std::make_unique<EventBus>([this](const std::string& owner, const std::string& error) {
        logger_->Logf(LogLevel::Error, LogCategory::Runtime, owner, "event handler threw: {}", error);
    });
    // Frame spike attribution: name the handler behind any event that blocks the game thread for > 4 ms.
    bus_->SetSlowHandlerReporter(
        [this, reported = 0](const std::string& owner, const char* eventType, double us) mutable {
            if (++reported <= kMaxSlowHandlerReports) {
                logger_->Logf(LogLevel::Info, LogCategory::Diagnostics, owner, "slow event handler: {:.1f} ms for {}",
                              us / 1000.0, eventType);
            }
        },
        4000.0);
    patches_ = std::make_unique<PatchManager>(*logger_);
    patches_->SetDisabled(SplitList(config.GetString("patches.disabled")));
    engine_ = std::make_unique<Engine>(*logger_, *patches_, *bus_);

    const std::filesystem::path exe = ModulePath(nullptr);
    const ExeIdentity identity = IdentifyExe(exe);
    const std::string& sha = identity.sha256;
    const auto build = FindBuildBySha256(sha);
    const BuildAdapterData* data = build ? adapters::FindAdapterData(build->id) : nullptr;
    if (!data) {
        logger_->Logf(LogLevel::Error, LogCategory::Compatibility, "",
                      "unsupported game executable {} (sha256 {}): SACore stays inactive, no hooks and no mods. "
                      "Supported builds are listed in docs/COMPATIBILITY.md.",
                      Utf8(exe.filename()), sha.empty() ? "<unreadable>" : sha);
        engine_->Configure({}, false);
        return;
    }
    buildId_ = std::string(build->id);
    logger_->Logf(LogLevel::Info, LogCategory::Compatibility, "", "game build {} ({}), exe {}{}", build->id,
                  build->description, Utf8(exe.filename()),
                  identity.largeAddressAware ? ", 4 GB address space (Large Address Aware flag set)" : ", 2 GB address space");

    stability_ = std::make_unique<Stability>(*logger_, *patches_, *bus_, recentLog_);
    StabilitySettings stabilitySettings;
    stabilitySettings.crashDumps = config.GetBool("stability.crash_dumps");
    stabilitySettings.skipDeviceSelection = config.GetBool("stability.skip_device_selection");
    stabilitySettings.testCrashAfterSeconds = static_cast<int>(config.GetInt("testing.crash_after_seconds"));
    stability_->Install(stabilitySettings, dataDirectory_ / L"crashes", [this] { return CrashContext(); });
    stability_->SetPackageFiles([this] {
        std::vector<std::pair<std::filesystem::path, std::string>> files{
            {logFile_, "logs/sacore.log"}, {dataDirectory_ / L"config" / L"core.toml", "config/core.toml"}};
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(dataDirectory_ / L"config" / L"mods", error)) {
            files.emplace_back(entry.path(), "config/mods/" + Utf8(entry.path().filename()));
        }
        for (const auto& entry : std::filesystem::directory_iterator(dataDirectory_ / L"diagnostics", error)) {
            files.emplace_back(entry.path(), "diagnostics/" + Utf8(entry.path().filename()));
        }
        return files;
    });
    adapter_ = std::make_unique<GameAdapter>(*data, CurrentModuleImage());
    WriteCapabilityReport();

    EngineSettings engineSettings;
    engineSettings.windowed = config.GetBool("window.windowed");
    engineSettings.clientWidth = static_cast<int>(config.GetInt("window.client_width"));
    engineSettings.runInBackground = config.GetBool("window.run_in_background");
    engineSettings.startMuted = config.GetBool("audio.start_muted");
    engineSettings.focusOnStart = config.GetBool("window.focus_on_start");
    engine_->Configure(engineSettings, true);

    GameSettings gameSettings;
    gameSettings.skipIntroMovies = config.GetBool("game.skip_intro_movies");
    gameSettings.autoStartNewGame = config.GetBool("testing.auto_start_new_game");
    gameSettings.giveMoneyOnStart = static_cast<int>(config.GetInt("testing.give_money_on_start"));
    gameSettings.logStateChanges = config.GetBool("testing.log_state_changes");
    game_ = std::make_unique<Game>(*logger_, *patches_, *bus_, *adapter_);
    game_->Setup(gameSettings);
    engine_->AddMessageFilter("core.game", [this](HWND, UINT message, WPARAM wParam, LPARAM, LRESULT& result) {
        result = 0;
        return game_->HandleMainLoopMessage(message, wParam);
    });

    saves_ = std::make_unique<SaveService>(*logger_, *patches_, *bus_, *adapter_, dataDirectory_);
    SaveSettings saveSettings;
    saveSettings.separateUserFiles = config.GetBool("saves.separate_user_files");
    saveSettings.backupsPerSlot = static_cast<int>(config.GetInt("saves.backups_per_slot"));
    early::DecideNow();
    const early::RedirectState& redirect = early::UserFilesRedirect();
    logger_->Logf(LogLevel::Debug, LogCategory::Diagnostics, "saves",
                  "user files redirect: hook {}, {} 'Personal' queries before init, decided {}, active {}",
                  redirect.hookInstalled, redirect.personalQueries, redirect.decided, redirect.active);
    saveSettings.redirectBase = redirect.active ? redirect.base : std::string{};
    saveSettings.redirectProblem = redirect.hookInstalled ? redirect.problem : "import of RegQueryValueExA not found";
    saves_->Install(saveSettings);

    harness_ = std::make_unique<Harness>(*logger_, *bus_, *engine_, *game_, *patches_, saves_.get(),
                                         dataDirectory_ / L"testing",
                                         logFile_,
                                         SACORE_VERSION_STRING, *buildId_);
    harness_->SetSettingsSetter([](const std::string& mod, const std::string& key, const std::string& value) {
        return ModApi::SetModSetting(mod, key, value);
    });
    if (!harness_->Start()) {
        harness_.reset();
    }

    bus_->Subscribe<events::DeviceCreated>("core", [this](const events::DeviceCreated&) { WritePatchReport(); });
    bus_->Subscribe<events::GameWindowDestroyed>("core", [this](const events::GameWindowDestroyed&) {
        OnGameWindowDestroyed();
    });

    if (!config.GetBool("mods.enabled")) {
        logger_->Log(LogLevel::Info, LogCategory::Mods, "", "mods disabled in core.toml ([mods] enabled = false)");
        return;
    }
    ModLoaderContext context;
    context.coreVersion = Version::Parse(SACORE_VERSION_STRING).value_or(Version{});
    context.coreVersionText = SACORE_VERSION_STRING;
    context.buildId = *buildId_;
    context.configDirectory = dataDirectory_ / L"config" / L"mods";
    modApi_ = std::make_unique<ModApi>(*logger_, *bus_, *engine_, *game_, *patches_, recentLog_);
    std::map<std::string, std::string> coreSettings;
    for (const auto& [key, value] : config.Values()) {
        coreSettings[key] = std::visit(
            [](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return v;
                } else {
                    return std::format("{}", v);
                }
            },
            value);
    }
    modApi_->SetCoreSettings(std::move(coreSettings));
    context.fillHost = [this](ModRecord& mod, SacoreHost& host) { modApi_->FillHost(mod, host); };
    context.onModStopped = [this](ModRecord& mod) { modApi_->ReleaseMod(mod); };
    mods_ = std::make_unique<ModLoader>(*logger_, context);
    modApi_->SetLoader(mods_.get());
    modApi_->SetSaves(saves_.get());
    mods_->LoadAll(dataDirectory_ / L"mods");

    std::vector<ContentSource> sources;
    for (const ModRecord* mod : mods_->LoadOrder()) {
        const std::filesystem::path root = mod->directory / L"content";
        auto files = ListContentFiles(root);
        if (!files.empty()) {
            sources.push_back({mod->id, root, std::move(files)});
        }
    }
    content_ = std::make_unique<ContentService>(*logger_, *patches_, gameDirectory_);
    content_->Apply(sources);
}

void CoreRuntime::OnDirect3DCreated(IDirect3D9* d3d) {
    if (engine_) {
        engine_->OnDirect3DCreated(d3d);
    }
}

void CoreRuntime::WriteCapabilityReport() const {
    std::string text = std::format("SACore {} capability report for build {}\n\n", SACORE_VERSION_STRING, *buildId_);
    for (const CapabilityStatus& c : adapter_->Capabilities()) {
        const std::string line = std::format("{:<26} {:<13} {}{}", c.id, ToString(c.state), c.description,
                                             c.reason.empty() ? "" : " [" + c.reason + "]");
        text += line + "\n";
        logger_->Logf(c.state == CapabilityState::Unsupported ? LogLevel::Warn : LogLevel::Info,
                      LogCategory::Compatibility, "capability", "{}", line);
    }
    text += "\nSymbols\n";
    for (const SymbolStatus& s : adapter_->Symbols()) {
        text += std::format("{:<42} {:#010x} {}{}\n", s.name, s.address, s.ok ? "ok: " : "FAILED: ", s.detail);
        if (!s.ok) {
            logger_->Logf(LogLevel::Warn, LogCategory::GameAdapter, "symbol", "{}: {}", s.name, s.detail);
        }
    }
    std::error_code error;
    std::filesystem::create_directories(dataDirectory_ / L"diagnostics", error);
    std::ofstream(dataDirectory_ / L"diagnostics" / L"capabilities.txt", std::ios::binary | std::ios::trunc) << text;
}

std::string CoreRuntime::CrashContext() const {
    std::string text = std::format("  SACore {} (mod API {}), build {}\n  SACore.asi sha256 {}\n", SACORE_VERSION_STRING,
                                   SACORE_API_VERSION, buildId_.value_or("unknown"), coreSha256_);
    text += std::format("  session {} s, {} frames{}\n", (GetTickCount64() - startTick_) / 1000,
                        engine_ ? engine_->FrameCount() : 0,
                        engine_ && engine_->CloseRequested() ? ", the game was closing (close requested)" : "");
    if (game_) {
        text += std::format("  game state {}, area {}\n", game_->State(), game_->CurrentArea());
    }
    if (mods_) {
        for (const auto& mod : mods_->Mods()) {
            text += std::format("  mod {} {} {}\n", mod->id,
                                mod->manifest ? mod->manifest->version.ToString() : std::string("?"), ToString(mod->state));
        }
    }
    for (const PatchRecord& patch : patches_->Patches()) {
        text += std::format("  patch {} ({}) {}\n", patch.info.id, patch.info.owner, ToString(patch.state));
    }
    return text;
}

void CoreRuntime::WritePatchReport() const {
    std::error_code error;
    std::filesystem::create_directories(dataDirectory_ / L"diagnostics", error);
    std::ofstream(dataDirectory_ / L"diagnostics" / L"patches.txt", std::ios::binary | std::ios::trunc)
        << patches_->StatusReport();
}

void CoreRuntime::OnGameWindowDestroyed() {
    logger_->Logf(LogLevel::Info, LogCategory::Core, "",
                  "game window closing after {} frames (SACore frame work: average {:.1f} us, max {:.0f} us; "
                  "{} background mouse grabs, {} background key reads, {} keyboard menu activations and {} focus "
                  "grabs blocked), shutting down mods",
                  engine_->FrameCount(), engine_->AverageFrameOverheadUs(), engine_->MaxFrameOverheadUs(),
                  engine_->BlockedMouseGrabs(), engine_->BlockedKeyReads(), engine_->BlockedMenuActivations(),
                  early::Focus().blockedCalls);
    WritePatchReport();
    if (modApi_) {
        modApi_->PublishShutdown();
    }
    if (mods_) {
        mods_->ShutdownAll();
    }
    logger_->Log(LogLevel::Info, LogCategory::Core, "", "shutdown complete");
    logger_->Flush();
}

}  // namespace sacore
