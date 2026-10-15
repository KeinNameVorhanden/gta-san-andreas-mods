// Autosave: saves after every passed mission and optionally every N minutes, into its own rotating files
// (GTASAas1.b ...), never into the 8 menu slots. A mission pass is seen as stat 147 (missions passed) going up; the
// save waits until the game is back in free roam (mission cleanup done, no cutscene, controls back), because a save
// made while a mission thread runs reloads broken.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <string>

namespace {

constexpr int kStatMissionsPassed = 147;

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
int g_baseline = -1;          // stat 147 at the last check (-1: take it fresh)
bool g_pending = false;
std::string g_reason;
ULONGLONG g_freeRoamSince = 0;
ULONGLONG g_lastSave = 0;     // also the start of the timer
ULONGLONG g_lastTry = 0;
bool g_saving = false;
std::string g_saveName;

void Message(const std::string& text) {
    if (g_host->ConfigBool("show_messages", true)) {
        g_game->ShowHelp(text.c_str());
    }
    g_host->Info("{}", text);
}

std::string LastMissionName() {
    const auto* name = g_host->GameVariable<char>("CStats::LastMissionPassedName");
    return name ? std::string(name, strnlen(name, 8)) : std::string{};
}

void Tick() {
    if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const int passed = g_game->StatInt(kStatMissionsPassed);
    if (g_baseline < 0 || passed < g_baseline) {
        g_baseline = passed;  // after a load or a new game
    } else if (passed > g_baseline) {
        g_baseline = passed;
        if (g_host->ConfigBool("after_mission", true)) {
            g_pending = true;
            g_reason = "mission passed (" + LastMissionName() + ")";
            g_host->Info("{}: autosave once back in free roam", g_reason);
        }
    }
    const int64_t interval = g_host->ConfigInt("interval_minutes", 0);
    if (interval > 0 && !g_pending && now - g_lastSave > static_cast<ULONGLONG>(interval) * 60000) {
        g_pending = true;
        g_reason = std::to_string(interval) + " minutes since the last autosave";
    }
    if (!g_pending || g_saving) {
        return;
    }
    if (!g_game->FreeRoam()) {
        g_freeRoamSince = 0;
        return;
    }
    if (g_freeRoamSince == 0) {
        g_freeRoamSince = now;
    }
    // The mission-end fade and the main script's reaction (phone calls, next trigger) need a moment.
    if (now - g_freeRoamSince < 2500 || now - g_lastTry < 5000) {
        return;
    }
    if (g_game->InVehicle() && !g_host->ConfigBool("allow_in_vehicle", false)) {
        return;  // waits until CJ is on foot
    }
    g_lastTry = now;
    const int slots = static_cast<int>(std::clamp<int64_t>(g_host->ConfigInt("slots", 3), 1, 9));
    g_saveName = g_host->NextRotatingSave("auto", slots);
    if (g_host->SaveGame(g_saveName)) {
        g_saving = true;
        g_host->Info("autosave to {} ({})", g_saveName, g_reason);
    } else {
        g_host->Info("autosave not possible here (interior), trying again");
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->HasSaveGame() || !g_host->Has("stats.read")) {
        g_host->Error("needs SACore 1.2.0 with save.extra_slots and stats.read on this build");
        return 1;
    }
    g_lastSave = GetTickCount64();
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent&) {
        g_baseline = -1;
        g_pending = false;
        g_lastSave = GetTickCount64();
    });
    g_host->On(SACORE_EVENT_SAVE_COMPLETED, [](const SacoreEvent& e) {
        if (!g_saving) {
            return;
        }
        g_saving = false;
        g_lastSave = GetTickCount64();
        if (e.success) {
            g_pending = false;
            Message("Autosaved (" + g_saveName.substr(4) + ").");
        } else {
            Message("Autosave FAILED (see the SACore log).");
        }
    });
    g_host->Info("ready: after missions {}, every {} min, {} files", g_host->ConfigBool("after_mission", true),
                 g_host->ConfigInt("interval_minutes", 0), g_host->ConfigInt("slots", 3));
    return 0;
}
