// Mission retry. While in free roam the mod keeps CJ's last
// position; when a mission starts it remembers that position, the nearest contact-point blip (radar blip type 5,
// the marker whose trigger launched the mission) and stat 147. When the mission ends without stat 147 rising
// (failed, wasted, busted) and CJ is back in free roam, the retry key puts CJ onto that marker the same way the
// teleport mod does, and the main script's own trigger starts the mission again. Missions that started indoors or
// without a marker nearby (phone calls, chains) are not offered.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr int kStatMissionsPassed = 147;
constexpr uint8_t kContactPointBlip = 5;

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
bool g_wasOnMission = false;
float g_lastFree[3] = {0, 0, 0};
bool g_haveLastFree = false;
float g_marker[3] = {0, 0, 0};
bool g_haveMarker = false;
int g_statAtStart = 0;
bool g_offer = false;
bool g_announced = false;
ULONGLONG g_failedAt = 0;
ULONGLONG g_freeSince = 0;
bool g_pendingTeleport = false;
char g_missionName[9] = {};

// Cutscene missions have no MISSION PASSED; ending without it is their normal end, not a fail.
constexpr const char* kCutsceneMissions[] = {"INTRO", "CESAR1", "CATALIN", "CATCUT", "FINALEA", "FINALEB"};

void ReadMissionName() {
    std::memset(g_missionName, 0, sizeof(g_missionName));
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    int guard = 0;
    for (uint8_t* script = head ? *head : nullptr; script && guard++ < 512; script = *reinterpret_cast<uint8_t**>(script)) {
        if (script[0xDC] == 1) {  // m_bIsMission
            std::memcpy(g_missionName, script + 0x08, 8);
            return;
        }
    }
}

// Free roam without the "controls disabled" test: after dying in the opening missions the game keeps a script
// control flag (0x20) set although CJ walks normally, and the retry must still be offered.
bool Roaming() {
    return g_host->GameState() == 9 && !g_game->OnMission() && !g_game->MiniGame() && !g_game->CutsceneRunning() &&
           g_game->PlayerState() == 0;
}

bool CutsceneMission() {
    for (const char* name : kCutsceneMissions) {
        if (_strnicmp(g_missionName, name, 8) == 0) {
            return true;
        }
    }
    return false;
}

bool FindMarker(const float origin[3], float out[3]) {
    const uintptr_t traces = g_host->Symbol("CRadar::ms_RadarTrace");
    float best = 15.0f * 15.0f;
    bool found = false;
    for (uint32_t i = 0; i < 175; ++i) {
        const auto* trace = reinterpret_cast<const uint8_t*>(traces + i * 0x28);
        if (!(trace[0x25] & 2) || ((trace[0x26] >> 2) & 0xF) != kContactPointBlip) {
            continue;
        }
        float pos[3];
        std::memcpy(pos, trace + 0x08, sizeof(pos));
        const float d = (pos[0] - origin[0]) * (pos[0] - origin[0]) + (pos[1] - origin[1]) * (pos[1] - origin[1]);
        if (d < best) {
            best = d;
            std::memcpy(out, pos, sizeof(pos));
            found = true;
        }
    }
    return found;
}

void Tick() {
    if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9) {
        return;
    }
    const bool onMission = g_game->OnMission();
    const auto player = g_host->Player();
    if (!onMission && Roaming() && player && player->area == 0) {
        std::memcpy(g_lastFree, player->position, sizeof(g_lastFree));
        g_haveLastFree = true;
    }
    if (onMission && !g_wasOnMission) {
        // Mission start: the contact marker lies where CJ stood a moment ago (the trigger blip is removed by many
        // missions, but the position of the free-roam frame before is the marker itself).
        g_offer = false;
        g_statAtStart = g_game->StatInt(kStatMissionsPassed);
        g_missionName[0] = '\0';
        g_haveMarker = g_haveLastFree && FindMarker(g_lastFree, g_marker);
        if (!g_haveMarker && g_haveLastFree) {
            std::memcpy(g_marker, g_lastFree, sizeof(g_marker));
            g_haveMarker = true;
        }
        g_haveMarker = g_haveMarker && player && player->area == 0;
    } else if (!onMission && g_wasOnMission) {
        const bool passed = g_game->StatInt(kStatMissionsPassed) > g_statAtStart || CutsceneMission();
        g_offer = !passed && g_haveMarker;
        g_announced = false;
        g_failedAt = GetTickCount64();
        g_freeSince = 0;
        g_host->Info("mission {} ended: {}{}", g_missionName, passed ? "passed or finished" : "failed",
                     g_offer ? ", retry offered" : "");
    }
    if (onMission && g_missionName[0] == '\0') {
        ReadMissionName();  // SCRIPT_NAME runs in the mission's first steps
    }
    g_wasOnMission = onMission;
    if (!g_offer) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_failedAt > static_cast<ULONGLONG>(g_host->ConfigInt("offer_seconds", 120)) * 1000) {
        g_offer = false;
        return;
    }
    if (!Roaming()) {
        g_freeSince = 0;
        static ULONGLONG s_reported = 0;
        if (now - g_failedAt > 15000 && s_reported != g_failedAt) {
            s_reported = g_failedAt;
            g_host->Info("retry waits for free roam: mission {}, mini game {}, cutscene {}, controls disabled {}, "
                         "player state {}", g_game->OnMission(), g_game->MiniGame(), g_game->CutsceneRunning(),
                         g_game->ControlsDisabled(), g_game->PlayerState());
            const auto pad = g_host->GameFunction<uint8_t*(__cdecl*)(int)>("CPad::GetPad")(0);
            g_host->Info("pad 0 DisablePlayerControls = {:#06x}", pad ? *reinterpret_cast<const uint16_t*>(pad + 0x10E) : 0);
        }
        return;
    }
    if (g_freeSince == 0) {
        g_freeSince = now;
    }
    if (!g_announced && now - g_freeSince > 3000) {
        g_announced = true;
        const int64_t key = g_host->ConfigInt("retry_key", VK_F4);
        const std::string name = key >= VK_F1 && key <= VK_F24 ? "F" + std::to_string(key - VK_F1 + 1) : "the retry key";
        const std::string text = "Mission failed. Press " + name + " to go back to the mission marker.";
        g_game->ShowHelp(text.c_str());
        g_host->Info("{}", text);
    }
    if (g_pendingTeleport) {
        g_pendingTeleport = false;
        uint8_t* ped = g_game->PlayerPed();
        if (!ped) {
            return;
        }
        using LoadFn = void(__cdecl*)(const float*);
        g_host->GameFunction<LoadFn>("CStreaming::LoadSceneCollision")(g_marker);
        g_host->GameFunction<LoadFn>("CStreaming::LoadScene")(g_marker);
        using SetFn = void(__thiscall*)(void*, void*, float, float, float, bool, bool);
        g_host->GameFunction<SetFn>("CRunningScript::SetCharCoordinates")(nullptr, ped, g_marker[0], g_marker[1],
                                                                         -100.0f, true, true);
        g_offer = false;
        g_host->Info("retry: back at the marker ({:.1f}, {:.1f})", g_marker[0], g_marker[1]);
        if (g_game->InVehicle()) {
            g_game->ShowHelp("Leave the vehicle: the mission starts on foot.");
        }
    }
}

void BindKeys() {
    g_host->ClearKeys();
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("retry_key", VK_F4)), [] {
        if (g_offer && g_announced && Roaming()) {
            g_pendingTeleport = true;  // executed in the game tick (LoadScene blocks)
        }
    });
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("world.teleport") || !g_host->Has("game.mission.read") || !g_host->Has("stats.read")) {
        g_host->Warn("mission retry is not available on this build");
        return 0;
    }
    BindKeys();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { BindKeys(); });
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent&) {
        g_offer = false;
        g_wasOnMission = false;
        g_haveLastFree = false;
    });
    g_host->Info("ready: retry key {}", g_host->ConfigInt("retry_key", VK_F4));
    return 0;
}
