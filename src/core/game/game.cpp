#include "game.h"

#include "adapters.h"
#include "events.h"
#include "game/game_layout.h"
#include "runtime/early_hooks.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <format>

namespace sacore {
namespace {

using GenericSaveFn = bool(__cdecl*)(int);
using GenericLoadFn = bool(__cdecl*)(bool*);
using RsEventHandlerFn = int(__cdecl*)(int event, void* parameter);
constexpr int kRsRwTerminate = 22;       // RsEvent rsRWTERMINATE (plugin-sdk skeleton.h)
constexpr int kRsEventProcessed = 1;

Game* g_game = nullptr;
GenericSaveFn g_origSave = nullptr;
GenericLoadFn g_origLoad = nullptr;
RsEventHandlerFn g_origRsEventHandler = nullptr;
void(__thiscall* g_origStartBrain)(void*, uint32_t, void*, uint32_t) = nullptr;
void(__cdecl* g_origGameProcess)() = nullptr;
int(__cdecl* g_origCdStreamSync)(int32_t) = nullptr;
uint8_t* const* g_cdChannels = nullptr;    // CdStream::gpChannels: 0x30 bytes per channel
const int32_t* g_cdThreaded = nullptr;     // CdStream::gbThreaded: reads run on the stream thread
std::atomic<uint32_t> g_cdMissedWakeups{0};
std::atomic<bool> g_cdReplacementUsed{false};  // the threaded path ran at least once (logged once)
constexpr ULONGLONG kSampleIntervalMs = 500;
constexpr int kAutoStartSamples = 6;  // 3 s in the frontend before the test trigger fires
constexpr UINT kStartNewGameMessage = WM_APP + 0x5A2;
using StartNewGameFn = void(__thiscall*)(void* menuManager);

bool SafeRead(uintptr_t address, void* out, size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
std::optional<T> Read(uintptr_t address) {
    T value{};
    if (address && SafeRead(address, &value, sizeof(T))) {
        return value;
    }
    return std::nullopt;
}

}  // namespace

Game::Game(Logger& logger, PatchManager& patches, EventBus& bus, const GameAdapter& adapter)
    : logger_(logger), patches_(patches), bus_(bus), adapter_(adapter) {
    g_game = this;
}

Game::~Game() {
    g_game = nullptr;
}

void Game::Setup(const GameSettings& settings) {
    settings_ = settings;
    if (settings_.skipIntroMovies) {
        SkipIntroMovies();
    }
    InstallSaveHooks();
    InstallExitCrashGuard();
    InstallScriptThreadGuard();
    InstallCdStreamSyncFix();
    SeparateCdStreamForParallelTests();
    InstallTickHook();
    bus_.Subscribe<events::FramePresented>("core.game", [this](const events::FramePresented&) { OnFrame(); });
    bus_.Subscribe<events::DeviceCreated>("core.game", [this](const events::DeviceCreated& e) { window_ = e.window; });
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game",
                 "testing options: auto start new game {}, give money on start {}, log state changes {}",
                 settings_.autoStartNewGame, settings_.giveMoneyOnStart, settings_.logStateChanges);
}

int Game::State() const {
    if (!adapter_.Available(adapters::kCapGameState)) {
        return -1;
    }
    return Read<int>(adapter_.Address(adapters::kGameState)).value_or(-1);
}

int Game::CurrentArea() const {
    if (!adapter_.Available(adapters::kCapAreaRead)) {
        return -1;
    }
    return Read<int>(adapter_.Address(adapters::kCurrArea)).value_or(-1);
}

std::optional<PlayerSnapshot> Game::Player() const {
    if (!adapter_.Available(adapters::kCapPlayerRead)) {
        return std::nullopt;
    }
    const uintptr_t players = adapter_.Address(adapters::kPlayers);
    const auto ped = Read<uintptr_t>(players + layout::kPlayerInfoPed);
    if (!ped || !*ped) {
        return std::nullopt;
    }
    PlayerSnapshot s;
    s.money = Read<int>(players + layout::kPlayerInfoMoney).value_or(0);
    s.health = Read<float>(*ped + layout::kPedHealth).value_or(0);
    s.armour = Read<float>(*ped + layout::kPedArmour).value_or(0);
    s.pedArea = Read<uint8_t>(*ped + layout::kEntityArea).value_or(0);
    const auto playerData = Read<uintptr_t>(*ped + layout::kPedPlayerData).value_or(0);
    const auto wanted = playerData ? Read<uintptr_t>(playerData).value_or(0) : 0;
    s.wantedLevel = wanted ? Read<int>(wanted + layout::kWantedLevel).value_or(0) : 0;
    const auto matrix = Read<uintptr_t>(*ped + layout::kEntityMatrix).value_or(0);
    const uintptr_t pos = matrix ? matrix + layout::kMatrixPos : *ped + layout::kPlacementPos;
    SafeRead(pos, s.position, sizeof(s.position));
    return s;
}

bool Game::OnMissionOrMiniGame() const {
    if (!adapter_.Available("game.mission.read")) {
        return false;
    }
    const int offset = Read<int>(adapter_.Address("CTheScripts::OnAMissionFlag")).value_or(0);
    const bool mission = offset != 0 && Read<int>(adapter_.Address("CTheScripts::ScriptSpace") + offset).value_or(0) == 1;
    return mission || Read<uint8_t>(adapter_.Address("CTheScripts::bMiniGameInProgress")).value_or(0) != 0;
}

bool Game::CutsceneRunning() const {
    return adapter_.Available("game.mission.read") &&
           Read<uint8_t>(adapter_.Address("CCutsceneMgr::ms_running")).value_or(0) != 0;
}

// Open (+0x5C m_bMenuActive) or requested (+0x33, "open next frame": while playing the request waits for the next
// screen fade, so a requested menu appears seconds later). Same offsets on both builds (1.0 0x53BC60, newsteam
// 0x54B920 set the request).
bool Game::PauseMenuActive() const {
    const uintptr_t menu = adapter_.Address(adapters::kMenuManager);
    return menu && (Read<uint8_t>(menu + 0x5C).value_or(0) != 0 || Read<uint8_t>(menu + 0x33).value_or(0) != 0);
}

bool Game::SetMoney(int money) {
    if (!adapter_.Available(adapters::kCapMoneyWrite) || !Player()) {
        return false;
    }
    *reinterpret_cast<volatile int*>(adapter_.Address(adapters::kPlayers) + layout::kPlayerInfoMoney) = money;
    return true;
}

bool Game::Teleport(float x, float y, float z) {
    const uintptr_t setCoordinates = adapter_.Address("CRunningScript::SetCharCoordinates");
    const uintptr_t players = adapter_.Address(adapters::kPlayers);
    void* ped = players ? *reinterpret_cast<void**>(players) : nullptr;
    if (!setCoordinates || !ped) {
        return false;
    }
    // __thiscall, this unused, ret 0x18
    using SetCharCoordinates = void(__thiscall*)(void*, void*, float, float, float, bool, bool);
    reinterpret_cast<SetCharCoordinates>(setCoordinates)(nullptr, ped, x, y, z, true, true);
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game", "testing: player teleported to ({:.1f}, {:.1f}, {:.1f})",
                 x, y, z);
    return true;
}

bool Game::SetWaypoint(float x, float y) {
    const uintptr_t setBlip = adapter_.Address("CRadar::SetCoordBlip");
    const uintptr_t menu = adapter_.Address(adapters::kMenuManager);
    if (!setBlip || !menu) {
        return false;
    }
    // int __cdecl(eBlipType, CVector, uint colour, eBlipDisplay, char* scriptName): BLIP_COORD (4), display both (3)
    using SetCoordBlip = int(__cdecl*)(int, float, float, float, unsigned, int, const char*);
    const int handle = reinterpret_cast<SetCoordBlip>(setBlip)(4, x, y, 0.0f, 0, 3, nullptr);
    *reinterpret_cast<int*>(menu + 0x2C) = handle;
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game", "testing: waypoint at ({:.1f}, {:.1f}), handle {:#x}",
                 x, y, handle);
    return handle != 0 && handle != -1;
}

bool Game::SetHealth(float health) {
    const uintptr_t players = adapter_.Address(adapters::kPlayers);
    auto* ped = players ? *reinterpret_cast<uint8_t**>(players) : nullptr;
    if (!ped) {
        return false;
    }
    *reinterpret_cast<float*>(ped + layout::kPedHealth) = health;
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game", "testing: player health set to {:.0f}", health);
    return true;
}

void Game::SkipIntroMovies() {
    if (!adapter_.Available(adapters::kCapIntroSkip)) {
        logger_.Log(LogLevel::Warn, LogCategory::GameAdapter, "game", "intro skip unavailable on this build");
        return;
    }
    if (State() != layout::kStartUp) {
        logger_.Logf(LogLevel::Warn, LogCategory::GameAdapter, "game",
                     "intro skip: game state is {} instead of 0 (start-up), not touching it", State());
        return;
    }
    const int initOnce = layout::kInitOnce;
    patches_.WriteMemory({"game.skip_intro", "core", "gGameState",
                          "the DirectShow intro movies never finish on some systems and block the game",
                          "game goes from start-up straight to init-once and the main menu",
                          "the logo and intro movies play (press a key to skip them)", false},
                         reinterpret_cast<void*>(adapter_.Address(adapters::kGameState)), &initOnce, sizeof(initOnce));
}

void Game::InstallSaveHooks() {
    if (!adapter_.Available(adapters::kCapSaveEvents)) {
        logger_.Log(LogLevel::Warn, LogCategory::GameAdapter, "game", "save events unavailable on this build");
        return;
    }
    patches_.InstallHook({"game.save_events", "core", "CGenericGameStorage::GenericSave",
                          "publish SaveStarted / SaveCompleted", "original save runs unchanged between the events",
                          "no save events; saving itself is unaffected"},
                         reinterpret_cast<void*>(adapter_.Address(adapters::kGenericSave)),
                         reinterpret_cast<void*>(&HookedGenericSave), reinterpret_cast<void**>(&g_origSave));
    patches_.InstallHook({"game.load_events", "core", "CGenericGameStorage::GenericLoad",
                          "publish LoadStarted / LoadCompleted", "original load runs unchanged between the events",
                          "no load events; loading itself is unaffected"},
                         reinterpret_cast<void*>(adapter_.Address(adapters::kGenericLoad)),
                         reinterpret_cast<void*>(&HookedGenericLoad), reinterpret_cast<void**>(&g_origLoad));
}

bool __cdecl Game::HookedGenericSave(int argument) {
    Game& self = *g_game;
    self.logger_.Log(LogLevel::Info, LogCategory::GameAdapter, "game", "SaveStarted");
    self.bus_.Publish(events::SaveStarted{});
    const bool ok = g_origSave(argument);
    self.logger_.Logf(ok ? LogLevel::Info : LogLevel::Error, LogCategory::GameAdapter, "game", "SaveCompleted ({})",
                      ok ? "success" : "FAILED");
    self.bus_.Publish(events::SaveCompleted{ok});
    return ok;
}

bool __cdecl Game::HookedGenericLoad(bool* out) {
    Game& self = *g_game;
    self.logger_.Log(LogLevel::Info, LogCategory::GameAdapter, "game", "LoadStarted");
    self.bus_.Publish(events::LoadStarted{});
    const bool ok = g_origLoad(out);
    // A failed load is normal when a new game starts (the game tries to load first) or a slot is empty.
    self.logger_.Logf(ok ? LogLevel::Info : LogLevel::Warn, LogCategory::GameAdapter, "game", "LoadCompleted ({})",
                      ok ? "success" : "no data loaded");
    self.bus_.Publish(events::LoadCompleted{ok});
    return ok;
}

// GTA SA can crash on exit (R20): after a new game with its intro mission, models are still alive when WinMain
// tears RenderWare down; their plugin destructors (animation, skin, game plugins) then run on data that is already
// gone, one after another. Everything worth keeping is written before that point (saves, settings), so when WinMain
// sends rsRWTERMINATE on its way out, the teardown is skipped and left to the process exit, which releases the
// memory and the Direct3D device anyway. Every other rsRWTERMINATE (device changes) runs unchanged.
void Game::InstallExitCrashGuard() {
    if (!adapter_.Available(adapters::kCapExitCrashGuard)) {
        return;
    }
    exitTeardownReturn_ = adapter_.Address(adapters::kExitRwTerminateCall) + 2 + 5;  // after push 0x16; call rel32
    patches_.InstallHook({"game.exit_crash_guard", "core", "RsEventHandler",
                          "avoid the crash on exit when models outlive RenderWare's teardown",
                          "WinMain's final rsRWTERMINATE is skipped; the process exit releases everything",
                          "the game may crash while closing (crash report, nothing lost)"},
                         reinterpret_cast<void*>(adapter_.Address(adapters::kRsEventHandler)),
                         reinterpret_cast<void*>(&HookedRsEventHandler), reinterpret_cast<void**>(&g_origRsEventHandler));
}

// GTA SA has 96 script threads. Every interior switch restarts the script brains of the objects around the player,
// and in a casino (Caligula's, the Four Dragons) that is some 55 threads at once on top of the story's ~37; when the
// free list is empty the game's StartNewScript dereferences null and the game crashes. With this guard such an object
// simply gets no brain (a slot machine stays idle until it streams in again) and the game keeps running.
void Game::InstallScriptThreadGuard() {
    if (!adapter_.Available(adapters::kCapScriptThreadGuard)) {
        return;
    }
    idleScripts_ = reinterpret_cast<void* const*>(adapter_.Address(adapters::kIdleScripts));
    patches_.InstallHook({"game.script_thread_guard", "core", "CScriptsForBrains::StartNewStreamedScriptBrain",
                          "avoid the crash when all 96 script threads are in use and an object brain starts",
                          "the brain is not started while no script thread is free",
                          "the game may crash in a casino interior when script threads run out"},
                         reinterpret_cast<void*>(adapter_.Address(adapters::kStartBrain)),
                         reinterpret_cast<void*>(&HookedStartBrain), reinterpret_cast<void**>(&g_origStartBrain));
}

void __fastcall Game::HookedStartBrain(void* brains, void*, uint32_t index, void* entity, uint32_t flag) {
    Game* self = g_game;
    if (self && self->idleScripts_ && *self->idleScripts_ == nullptr) {
        ++self->brainsSkipped_;
        const uint64_t now = GetTickCount64();
        if (now - self->brainSkipLogged_ > 5000) {
            self->brainSkipLogged_ = now;
            self->logger_.Logf(LogLevel::Warn, LogCategory::GameAdapter, "game",
                               "all script threads in use: object brain {} not started ({} so far, "
                               "game.script_thread_guard)", index & 0xFF, self->brainsSkipped_);
        }
        return;
    }
    g_origStartBrain(brains, index, entity, flag);
}

// CdStreamSync race (both builds): the game waits for a read with
//   if (channel.sectorsToRead != 0) { channel.locked = 1; WaitForSingleObject(channel.semaphore, INFINITE); }
// while the stream thread finishes it with
//   channel.sectorsToRead = 0; if (channel.locked) ReleaseSemaphore(channel.semaphore);
// When the read ends between the game's check and its "locked = 1", nobody releases the semaphore and the game
// waits forever (observed: a 1.0 US copy frozen in a load, read done, channel locked, stream thread idle). The
// replacement waits in short steps and checks the read itself, so a missed release costs a few milliseconds.
int __cdecl Game::HookedCdStreamSync(int32_t channelIndex) {
    if (!g_cdChannels || !*g_cdChannels || !g_cdThreaded || *g_cdThreaded == 0) {
        return g_origCdStreamSync(channelIndex);  // overlapped reads on the game's own thread: unchanged
    }
    g_cdReplacementUsed.store(true, std::memory_order_relaxed);
    uint8_t* channel = *g_cdChannels + static_cast<size_t>(channelIndex) * 0x30;
    const auto sectorsToRead = reinterpret_cast<volatile int32_t*>(channel + 0x04);
    const auto locked = reinterpret_cast<volatile uint8_t*>(channel + 0x0D);
    const auto inUse = reinterpret_cast<volatile uint8_t*>(channel + 0x0E);
    const HANDLE semaphore = *reinterpret_cast<HANDLE*>(channel + 0x14);
    if (*sectorsToRead != 0) {
        *locked = 1;
        while (*sectorsToRead != 0) {
            if (WaitForSingleObject(semaphore, 10) == WAIT_TIMEOUT && *sectorsToRead == 0) {
                // Done, but not signalled yet (or never: the race). A late release only wakes a later wait early,
                // which then checks the read again.
                g_cdMissedWakeups.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    *inUse = 0;
    return *reinterpret_cast<volatile int32_t*>(channel + 0x10);
}

void Game::InstallCdStreamSyncFix() {
    if (!adapter_.Available(adapters::kCapCdStreamSync)) {
        return;
    }
    g_cdChannels = reinterpret_cast<uint8_t* const*>(adapter_.Address(adapters::kCdStreamChannels));
    g_cdThreaded = reinterpret_cast<const int32_t*>(adapter_.Address(adapters::kCdStreamThreaded));
    patches_.InstallHook({"game.cdstream_sync", "core", "CdStreamSync",
                          "no freeze when a stream read ends while the game starts waiting for it",
                          "the wait for a read re-checks the read every 10 ms",
                          "the game can freeze for good while loading (rare race in the stream code)"},
                         reinterpret_cast<void*>(adapter_.Address(adapters::kCdStreamSync)),
                         reinterpret_cast<void*>(&HookedCdStreamSync), reinterpret_cast<void**>(&g_origCdStreamSync));
}

// The game's CdStreamInit creates its request semaphore with a fixed name ("CdStream"), so two running copies share
// it: one game's "request ready" wakes the other game's stream thread, the request is lost and CdStreamSync waits
// forever (both games freeze while loading). Only test runs start several copies at once (the instance check refuses
// a second one otherwise). When the early hook could not give the semaphore a private name (the 1.0 US build creates
// it before SACore loads), the idle stream thread is replaced by one that waits on a private semaphore.
void Game::SeparateCdStreamForParallelTests() {
    const early::InstanceState& instance = early::Instance();
    if (!instance.parallelAllowed || instance.cdStreamRenamed ||
        !adapter_.Available(adapters::kCapParallelTestCopies)) {
        return;
    }
    auto& semaphore = *reinterpret_cast<HANDLE*>(adapter_.Address(adapters::kCdStreamSemaphore));
    auto& thread = *reinterpret_cast<HANDLE*>(adapter_.Address(adapters::kCdStreamThreadHandle));
    auto& threadId = *reinterpret_cast<DWORD*>(adapter_.Address(adapters::kCdStreamThreadId));
    const auto head = reinterpret_cast<volatile int32_t*>(adapter_.Address(adapters::kCdStreamQueueHead));
    const auto tail = reinterpret_cast<volatile int32_t*>(adapter_.Address(adapters::kCdStreamQueueTail));
    if (!semaphore || !thread || SuspendThread(thread) == static_cast<DWORD>(-1)) {
        return;
    }
    if (*head != *tail) {  // a request is queued: the thread is working, leave it alone
        ResumeThread(thread);
        logger_.Log(LogLevel::Warn, LogCategory::GameAdapter, "game",
                    "CdStream semaphore shared with other copies (stream busy at start-up); run copies one at a time");
        return;
    }
    const int priority = GetThreadPriority(thread);
    TerminateThread(thread, 0);  // blocked in WaitForSingleObject on the shared semaphore, holding nothing
    WaitForSingleObject(thread, 1000);
    CloseHandle(thread);
    CloseHandle(semaphore);
    semaphore = CreateSemaphoreA(nullptr, 0, 5, nullptr);
    thread = CreateThread(nullptr, 0x10000,
                          reinterpret_cast<LPTHREAD_START_ROUTINE>(adapter_.Address(adapters::kCdStreamThread)),
                          nullptr, CREATE_SUSPENDED, &threadId);
    if (thread) {
        SetThreadPriority(thread, priority);
        ResumeThread(thread);
    }
    logger_.Logf(thread && semaphore ? LogLevel::Info : LogLevel::Error, LogCategory::GameAdapter, "game",
                 "test run: CdStream uses a private semaphore now (stream thread {}), so copies of this build can run "
                 "side by side", threadId);
}

int __cdecl Game::HookedRsEventHandler(int event, void* parameter) {
    Game* self = g_game;
    if (self && event == kRsRwTerminate &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == self->exitTeardownReturn_) {
        self->logger_.Log(LogLevel::Info, LogCategory::GameAdapter, "game",
                          "exit: RenderWare teardown left to the process exit (game.exit_crash_guard)");
        self->logger_.Flush();
        return kRsEventProcessed;
    }
    return g_origRsEventHandler(event, parameter);
}

// Game logic tick for mods (SACORE_EVENT_GAME_TICK): world changes belong here, not in the Present hook, because
// streaming and world lists are only consistent between logic steps.
void Game::InstallTickHook() {
    if (!adapter_.Available(adapters::kCapGameTick)) {
        return;
    }
    patches_.InstallHook({"game.tick", "core", "CGame::Process", "game logic tick event for mods",
                          "GameTick published after every CGame::Process", "mods get no game logic tick"},
                         reinterpret_cast<void*>(adapter_.Address("CGame::Process")),
                         reinterpret_cast<void*>(&HookedGameProcess), reinterpret_cast<void**>(&g_origGameProcess));
}

void __cdecl Game::HookedGameProcess() {
    g_origGameProcess();
    if (g_game) {
        g_game->bus_.Publish(events::GameTick{});
    }
}

bool Game::HandleMainLoopMessage(UINT message, WPARAM wParam) {
    if (message != kStartNewGameMessage) {
        return false;
    }
    StartNewGameFromMenu(wParam != 0);
    return true;
}

bool Game::RequestNewGame(bool whilePlaying) {
    return window_ && PostMessageW(window_, kStartNewGameMessage, whilePlaying ? 1 : 0, 0);
}

void Game::StartNewGameFromMenu(bool whilePlaying) {
    const int expected = whilePlaying ? layout::kPlayingGame : layout::kFrontend;
    if (State() != expected || !adapter_.Available(adapters::kCapStartNewGame)) {
        logger_.Logf(LogLevel::Warn, LogCategory::GameAdapter, "game", "testing: new game not started (state {})",
                     State());
        return;
    }
    const uintptr_t menu = adapter_.Address(adapters::kMenuManager);
    const auto routine = reinterpret_cast<StartNewGameFn>(adapter_.Address(adapters::kStartNewGameRoutine));
    routine(reinterpret_cast<void*>(menu));
    // The game asks for its main menu at start-up ("open next frame", +0x33) and opens it on the first front-end
    // frame. With the monitors in power save no front-end frame is drawn, so the request was still pending when this
    // routine skipped the menu, and the pause menu opened at the first screen fade of the new game (every test game
    // started at night stood still).
    auto& menuRequested = *reinterpret_cast<volatile uint8_t*>(menu + 0x33);
    const bool stale = menuRequested != 0;
    menuRequested = 0;
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game",
                 "testing: new game started through the menu routine (as if chosen in the {} menu){}",
                 whilePlaying ? "pause" : "main", stale ? "; pending menu request dropped" : "");
}

void Game::OnFrame() {
    const ULONGLONG now = GetTickCount64();
    if (now - lastSample_ < kSampleIntervalMs) {
        return;
    }
    lastSample_ = now;
    if (!cdReplacementLogged_ && g_cdReplacementUsed.load(std::memory_order_relaxed)) {
        cdReplacementLogged_ = true;
        logger_.Log(LogLevel::Info, LogCategory::GameAdapter, "game",
                    "stream reads are waited for by the CdStreamSync replacement (game.cdstream_sync)");
    }
    if (const uint32_t missed = g_cdMissedWakeups.load(std::memory_order_relaxed); missed != cdMissedLogged_) {
        cdMissedLogged_ = missed;
        logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game",
                     "stream read finished without its wake-up, the game went on ({} so far, game.cdstream_sync)",
                     missed);
    }
    const int state = State();
    if (state != lastState_) {
        logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game", "game state {} -> {}", lastState_, state);
        bus_.Publish(events::GameStateChanged{lastState_, state});
        lastState_ = state;
    }
    // While playing, the pause menu (FrontEndMenuManager.m_bMenuActive, +0x5C) stops the game logic but frames go
    // on; logged so a stalled test or mod can be told apart from a paused game.
    if (const uintptr_t menu = adapter_.Address(adapters::kMenuManager); menu && state == layout::kPlayingGame) {
        const int active = *reinterpret_cast<const volatile bool*>(menu + 0x5C) ? 1 : 0;
        if (active != lastMenuActive_ && lastMenuActive_ != -1) {
            logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game", "pause menu {}", active ? "opened" : "closed");
        }
        lastMenuActive_ = active;
    }

    if (settings_.autoStartNewGame && !autoStartDone_ && adapter_.Available(adapters::kCapStartNewGame)) {
        frontendSamples_ = state == layout::kFrontend ? frontendSamples_ + 1 : 0;
        if (frontendSamples_ >= kAutoStartSamples) {
            // Run the menu's own "new game" routine from the message loop (never inside a frame). Writing
            // gGameState = 8 directly skipped the main loop's first-start bookkeeping, so a later "new game" from
            // the pause menu re-initialised the level without shutting it down and crashed (R19).
            if (RequestNewGame(false)) {
                autoStartDone_ = true;
            }
        }
    }

    const auto player = state == layout::kPlayingGame ? Player() : std::nullopt;
    if (settings_.giveMoneyOnStart != 0 && !moneyGiven_ && player) {
        const long long target = static_cast<long long>(player->money) + settings_.giveMoneyOnStart;
        const int clamped = static_cast<int>(std::clamp(target, 0LL, 999'999'999LL));
        moneyGiven_ = SetMoney(clamped);
        logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game", "testing: money {} -> {} ({})", player->money,
                     clamped, moneyGiven_ ? "written" : "unavailable");
    }

    if (settings_.logStateChanges && player) {
        const bool changed =
            !lastLogged_ || std::lround(lastLogged_->health) != std::lround(player->health) ||
            std::lround(lastLogged_->armour) != std::lround(player->armour) || lastLogged_->money != player->money ||
            lastLogged_->wantedLevel != player->wantedLevel || lastLogged_->pedArea != player->pedArea ||
            std::hypot(lastLogged_->position[0] - player->position[0], lastLogged_->position[1] - player->position[1]) >
                100.0f;
        if (changed) {
            logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "game",
                         "player: health {:.0f} armour {:.0f} money {} wanted {} area {} (world {}) pos ({:.1f}, "
                         "{:.1f}, {:.1f})",
                         player->health, player->armour, player->money, player->wantedLevel, player->pedArea,
                         CurrentArea(), player->position[0], player->position[1], player->position[2]);
            lastLogged_ = player;
        }
    }
}

}  // namespace sacore
