// Waypoint teleport and safehouse fast travel.
// Target: the map waypoint (FrontEndMenuManager.m_nTargetBlipIndex at +0x2C, a coordinate blip in
// CRadar::ms_RadarTrace) or the save-game blips (sprite 35) the scripts create for every safehouse CJ owns.
// The move runs in the game tick: CStreaming::LoadSceneCollision + LoadScene around the target, then
// CRunningScript::SetCharCoordinates with z = -100 (the function finds the ground itself, moves the vehicle when
// CJ is in one and takes the gang members along), exactly what SET_CHAR_COORDINATES does. Outdoors only.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint8_t kSaveHouseSprite = 35;

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
bool g_pending = false;
float g_target[3] = {0, 0, 0};
bool g_clearWaypoint = false;
std::string g_targetName;
int g_lastHouse = -1;

const uint8_t* Trace(uint32_t index) {
    return reinterpret_cast<const uint8_t*>(g_host->Symbol("CRadar::ms_RadarTrace") + index * 0x28);
}

bool InUse(const uint8_t* trace) {
    return (trace[0x25] & 2) != 0;
}

bool Allowed() {
    if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9 || g_game->PlayerState() != 0 ||
        g_game->CutsceneRunning()) {
        return false;
    }
    if (g_game->OnMission() && !g_host->ConfigBool("allow_during_missions", false)) {
        g_game->ShowHelp("Teleport is off during missions.");
        return false;
    }
    const auto player = g_host->Player();
    if (!player || player->area != 0) {
        g_game->ShowHelp("Teleport only works outdoors.");
        return false;
    }
    return true;
}

void ToWaypoint() {
    if (!Allowed()) {
        return;
    }
    const uint32_t handle = *reinterpret_cast<const uint32_t*>(g_host->Symbol("FrontEndMenuManager") + 0x2C);
    const uint32_t index = handle & 0xFFFF;
    const uint8_t* trace = index < 175 ? Trace(index) : nullptr;
    if (handle == 0 || !trace || *reinterpret_cast<const uint16_t*>(trace + 0x14) != (handle >> 16) || !InUse(trace)) {
        g_game->ShowHelp("Set a waypoint on the map first.");
        return;
    }
    std::memcpy(g_target, trace + 0x08, sizeof(g_target));
    g_clearWaypoint = true;
    g_targetName = "the waypoint";
    g_pending = true;
}

void ToNextSafehouse() {
    if (!Allowed()) {
        return;
    }
    std::vector<uint32_t> houses;
    for (uint32_t i = 0; i < 175; ++i) {
        const uint8_t* trace = Trace(i);
        if (InUse(trace) && trace[0x24] == kSaveHouseSprite) {
            houses.push_back(i);
        }
    }
    if (houses.empty()) {
        g_game->ShowHelp("No safehouse on the map yet.");
        return;
    }
    // The next one after the last visited (blip order), so repeated presses go round all of them.
    uint32_t chosen = houses.front();
    for (uint32_t house : houses) {
        if (static_cast<int>(house) > g_lastHouse) {
            chosen = house;
            break;
        }
    }
    g_lastHouse = static_cast<int>(chosen);
    std::memcpy(g_target, Trace(chosen) + 0x08, sizeof(g_target));
    g_clearWaypoint = false;
    g_targetName = "safehouse " + std::to_string(
        std::find(houses.begin(), houses.end(), chosen) - houses.begin() + 1) + " of " + std::to_string(houses.size());
    g_pending = true;
}

void Tick() {
    if (!g_pending) {
        return;
    }
    g_pending = false;
    uint8_t* ped = g_game->PlayerPed();
    if (!ped) {
        return;
    }
    using LoadFn = void(__cdecl*)(const float*);
    float position[3] = {g_target[0], g_target[1], g_target[2]};
    g_host->GameFunction<LoadFn>("CStreaming::LoadSceneCollision")(position);
    g_host->GameFunction<LoadFn>("CStreaming::LoadScene")(position);
    using SetFn = void(__thiscall*)(void*, void*, float, float, float, bool, bool);
    g_host->GameFunction<SetFn>("CRunningScript::SetCharCoordinates")(nullptr, ped, g_target[0], g_target[1], -100.0f,
                                                                     true, true);
    if (g_clearWaypoint) {
        auto* handle = reinterpret_cast<uint32_t*>(g_host->Symbol("FrontEndMenuManager") + 0x2C);
        g_host->GameFunction<void(__cdecl*)(uint32_t)>("CRadar::ClearBlip")(*handle);
        *handle = 0;
    }
    g_host->Info("teleported to {} ({:.1f}, {:.1f})", g_targetName, g_target[0], g_target[1]);
}

void BindKeys() {
    g_host->ClearKeys();
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("waypoint_key", VK_F2)), &ToWaypoint);
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("fast_travel_key", VK_F3)), &ToNextSafehouse);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("world.teleport")) {
        g_host->Warn("teleport is not available on this build");
        return 0;
    }
    BindKeys();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { BindKeys(); });
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->Info("ready: waypoint key {}, fast travel key {}", g_host->ConfigInt("waypoint_key", VK_F2),
                 g_host->ConfigInt("fast_travel_key", VK_F3));
    return 0;
}
