// Density and draw distance. CPopulation::PedDensityMultiplier
// and CCarCtrl::CarDensityMultiplier (default 1.0) are reset by the game on new game and load and set by cutscenes
// and missions, so the mod re-applies them every tick outside cutscenes (and missions, unless switched off).
// Draw distance: CRenderer::ms_lodDistScale, which the menu slider writes (0.925 .. 1.8); values above the slider's
// maximum need more streaming memory (streaming_memory mod) to avoid pop-in.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
bool g_wroteLod = false;
float g_menuLod = 0.0f;

void Tick() {
    if (!g_host->ConfigBool("enabled", true) || g_game->CutsceneRunning() ||
        (g_host->ConfigBool("respect_missions", true) && g_game->OnMission())) {
        return;
    }
    const auto ped = static_cast<float>(g_host->ConfigDouble("ped_density", 1.0));
    const auto cars = static_cast<float>(g_host->ConfigDouble("traffic_density", 1.0));
    if (ped != 1.0f) {
        *g_host->GameVariable<float>("CPopulation::PedDensityMultiplier") = ped;
    }
    if (cars != 1.0f) {
        *g_host->GameVariable<float>("CCarCtrl::CarDensityMultiplier") = cars;
    }
    auto* lod = g_host->GameVariable<float>("CRenderer::ms_lodDistScale");
    const auto distance = static_cast<float>(g_host->ConfigDouble("draw_distance", 0.0));
    if (distance > 0.0f) {
        if (!g_wroteLod) {
            g_menuLod = *lod;
            g_wroteLod = true;
        }
        *lod = distance;
    } else if (g_wroteLod) {
        *lod = g_menuLod;  // back to the slider's value
        g_wroteLod = false;
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("world.density")) {
        g_host->Warn("density settings are not available on this build");
        return 0;
    }
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->Info("ready: peds x{}, traffic x{}, draw distance {}", g_host->ConfigDouble("ped_density", 1.0),
                 g_host->ConfigDouble("traffic_density", 1.0), g_host->ConfigDouble("draw_distance", 0.0));
    return 0;
}
