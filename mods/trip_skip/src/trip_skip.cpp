// Trip skip. Classic SA has a complete trip-skip system that some
// missions use after a failed attempt: CGameLogic::SetUpSkip(pos, heading, afterMission, vehicle, finishedByScript)
// offers it, the HUD shows the prompt, the game fades, streams the target, moves CJ's vehicle and passes game time.
// The mod offers it on long drives during missions: exactly one yellow destination blip (coordinate blip, type 4,
// shown on the radar), CJ in a road vehicle outdoors, no skip of the mission's own (SkipState 0), far enough away.
// The skip ends stop_before metres short of the blip so the mission's arrival check still runs normally. The game
// cancels the offer itself after 35 s or within 25 m; it is offered again after that.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

constexpr uint8_t kCoordBlip = 4;

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
ULONGLONG g_lastOffer = 0;

// The single destination blip, or false when there is none or more than one.
bool Destination(float out[3]) {
    const uintptr_t traces = g_host->Symbol("CRadar::ms_RadarTrace");
    // The player's own map waypoint is a coordinate blip too; it is not a mission destination.
    const uintptr_t menu = g_host->Symbol("FrontEndMenuManager");
    const uint32_t waypointHandle = menu ? *reinterpret_cast<const uint32_t*>(menu + 0x2C) : 0;
    const uint32_t waypoint = waypointHandle ? (waypointHandle & 0xFFFF) : UINT32_MAX;
    int found = 0;
    for (uint32_t i = 0; i < 175; ++i) {
        if (i == waypoint) {
            continue;
        }
        const auto* trace = reinterpret_cast<const uint8_t*>(traces + i * 0x28);
        const bool inUse = (trace[0x25] & 2) != 0, shortRange = (trace[0x25] & 4) != 0;
        const uint8_t display = trace[0x26] & 3, type = (trace[0x26] >> 2) & 0xF;
        if (inUse && !shortRange && display != 0 && type == kCoordBlip) {
            std::memcpy(out, trace + 0x08, 12);
            ++found;
        }
    }
    return found == 1;
}

void Tick() {
    if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9 || !g_game->OnMission() ||
        !g_game->InVehicle() || g_game->CutsceneRunning() || g_game->ControlsDisabled() || g_game->PlayerState() != 0) {
        return;
    }
    if (*g_host->GameVariable<int>("CGameLogic::SkipState") != 0) {
        return;  // offered already, running, or the mission's own skip
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastOffer < 40000) {
        return;  // the game withdraws an offer after 35 s; offer again after that
    }
    if (g_host->ConfigBool("only_when_not_wanted", true) && g_game->WantedLevel() > 0) {
        return;
    }
    const auto player = g_host->Player();
    float target[3];
    if (!player || player->area != 0 || !Destination(target)) {
        return;
    }
    const float dx = target[0] - player->position[0], dy = target[1] - player->position[1];
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < static_cast<float>(g_host->ConfigInt("min_distance", 400))) {
        return;
    }
    const float back = static_cast<float>(g_host->ConfigInt("stop_before", 40)) / distance;
    const float x = target[0] - dx * back, y = target[1] - dy * back;
    // Script heading in degrees, 0 = north, counter-clockwise (SET_UP_SKIP passes the script value unchanged).
    const float heading = std::atan2(-dx, dy) * 57.2957795f;
    using SetUpSkip = void(__cdecl*)(float, float, float, float, bool, void*, bool);
    g_host->GameFunction<SetUpSkip>("CGameLogic::SetUpSkip")(x, y, target[2], heading, false, nullptr, false);
    g_lastOffer = now;
    g_host->Info("trip skip offered: {:.0f} m to ({:.0f}, {:.0f})", distance, target[0], target[1]);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("world.trip_skip") || !g_host->Has("game.mission.read")) {
        g_host->Warn("trip skip is not available on this build");
        return 0;
    }
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent&) { g_lastOffer = 0; });
    g_host->Info("ready: trip skip from {} m", g_host->ConfigInt("min_distance", 400));
    return 0;
}
