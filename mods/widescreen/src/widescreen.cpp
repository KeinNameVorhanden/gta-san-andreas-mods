// Widescreen FOV. CDraw::SetFOV stores the camera's field of
// view (45 degrees by default) without any aspect correction, so on a wide screen the 4:3 width is kept and the
// picture is cut at the top and bottom. The hook widens it (Hor+): fov' = 2 * atan(tan(fov / 2) * aspect / (4/3)),
// with the aspect of the game window as shown (newsteam stores CDraw::ms_fAspectRatio divided by 1.1, a pixel aspect
// factor, and SilentPatch replaces it at run time, so it is not used). Works on both builds.
// HUD and radar proportions are not changed: they have only been read statically so far, and on newsteam
// SilentPatch may already correct them; that needs a look at the running game first.
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cmath>
#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
void(__cdecl* g_origSetFov)(float) = nullptr;
bool g_logged = false;

void __cdecl HookedSetFov(float fov) {
    if (g_host->ConfigBool("enabled", true)) {
        // The aspect of the picture as shown: the game window's client area. CDraw::ms_fAspectRatio is not reliable
        // for this (newsteam's own table stores values divided by 1.1, and SilentPatch replaces them at run time).
        float aspect = *g_host->GameVariable<float>("CDraw::ms_fAspectRatio");
        RECT client{};
        if (auto* window = static_cast<HWND>(g_host->Raw()->game_window(g_host->Raw()));
            window && GetClientRect(window, &client) && client.bottom > 0) {
            aspect = static_cast<float>(client.right) / static_cast<float>(client.bottom);
        }
        constexpr float kRad = 3.14159265f / 180.0f;
        if (g_host->ConfigBool("hor_plus", true) && aspect > 4.0f / 3.0f + 0.01f && fov > 1.0f && fov < 170.0f) {
            const float wide = 2.0f * std::atan(std::tan(fov * kRad / 2.0f) * aspect / (4.0f / 3.0f)) / kRad;
            if (!g_logged) {
                g_host->Info("FOV {:.1f} -> {:.1f} for aspect {:.3f}", fov, wide, aspect);
                g_logged = true;
            }
            fov = wide;
        }
        fov += static_cast<float>(g_host->ConfigDouble("extra_fov", 0.0));
        if (fov > 150.0f) {
            fov = 150.0f;
        }
    }
    g_origSetFov(fov);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->Has("camera.fov")) {
        g_host->Warn("FOV fix is not available on this build");
        return 0;
    }
    using FovFn = void __cdecl(float);
    if (!g_host->Hook<FovFn>("set_fov", "CDraw::SetFOV", "Hor+ field of view",
                             g_host->GameFunction<FovFn*>("CDraw::SetFOV"), &HookedSetFov, &g_origSetFov)) {
        g_host->Error("could not hook CDraw::SetFOV");
        return 1;
    }
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { g_logged = false; });
    g_host->Info("ready: Hor+ {}, extra {} degrees", g_host->ConfigBool("hor_plus", true),
                 g_host->ConfigDouble("extra_fov", 0.0));
    return 0;
}
