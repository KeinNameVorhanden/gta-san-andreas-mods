// Game layer: typed access to game state through the adapter, game-level features and game events.
#pragma once

#include "event_bus.h"
#include "game/adapter.h"
#include "logger.h"
#include "patch_manager.h"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace sacore {

struct PlayerSnapshot {
    float health = 0;
    float armour = 0;
    int money = 0;
    int wantedLevel = 0;
    float position[3] = {0, 0, 0};
    int pedArea = 0;
};

struct GameSettings {
    bool skipIntroMovies = true;
    // Test automation, off by default.
    bool autoStartNewGame = false;
    int giveMoneyOnStart = 0;
    bool logStateChanges = false;
};

class Game {
public:
    Game(Logger& logger, PatchManager& patches, EventBus& bus, const GameAdapter& adapter);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // Called on the game thread before the main loop starts (gGameState still 0).
    void Setup(const GameSettings& settings);

    const GameAdapter& Adapter() const { return adapter_; }
    int State() const;                              // -1 if unavailable
    int CurrentArea() const;                        // -1 if unavailable
    std::optional<PlayerSnapshot> Player() const;   // nullopt when there is no player ped
    // Test harness: puts the player (or the player's vehicle) at x, y, z like SET_CHAR_COORDINATES. Game thread,
    // between logic steps. False when the build lacks the function.
    bool Teleport(float x, float y, float z);
    // Test harness: a map waypoint at x, y (coordinate blip and FrontEndMenuManager.m_nTargetBlipIndex).
    bool SetWaypoint(float x, float y);
    bool SetHealth(float health);
    bool SetMoney(int money);                       // false if unavailable
    bool OnMissionOrMiniGame() const;               // false if unknown on this build
    bool CutsceneRunning() const;                   // false if unknown on this build
    bool PauseMenuActive() const;                   // pause menu open or requested; false if unknown
    // Window messages the game layer runs from the game's message loop (outside any frame); true if handled.
    bool HandleMainLoopMessage(UINT message, WPARAM wParam);
    // Runs the menu's "new game" routine from the message loop: from the main menu, or while playing exactly
    // like "Start new game" in the pause menu. False if there is no game window yet.
    bool RequestNewGame(bool whilePlaying);

private:
    static bool __cdecl HookedGenericSave(int argument);
    static bool __cdecl HookedGenericLoad(bool* out);
    static int __cdecl HookedRsEventHandler(int event, void* parameter);
    static void __cdecl HookedGameProcess();
    void InstallTickHook();
    void InstallExitCrashGuard();
    void InstallScriptThreadGuard();
    void InstallCdStreamSyncFix();
    static int __cdecl HookedCdStreamSync(int32_t channelIndex);
    void SeparateCdStreamForParallelTests();
    static void __fastcall HookedStartBrain(void* brains, void* edx, uint32_t index, void* entity, uint32_t flag);
    void InstallSaveHooks();
    void SkipIntroMovies();
    void OnFrame();
    void StartNewGameFromMenu(bool whilePlaying);

    Logger& logger_;
    PatchManager& patches_;
    EventBus& bus_;
    const GameAdapter& adapter_;
    GameSettings settings_;
    int lastState_ = -1;
    int frontendSamples_ = 0;
    bool autoStartDone_ = false;
    HWND window_ = nullptr;
    bool moneyGiven_ = false;
    uintptr_t exitTeardownReturn_ = 0;
    void* const* idleScripts_ = nullptr;  // CTheScripts::pIdleScripts (script thread guard)
    uint64_t brainsSkipped_ = 0;
    uint64_t brainSkipLogged_ = 0;
    bool cdReplacementLogged_ = false;
    uint32_t cdMissedLogged_ = 0;  // stream wake-ups the CdStreamSync fix stood in for, already logged
    uint64_t lastSample_ = 0;
    int lastMenuActive_ = -1;  // FrontEndMenuManager.m_bMenuActive while playing (-1 unknown)
    std::optional<PlayerSnapshot> lastLogged_;
};

}  // namespace sacore
