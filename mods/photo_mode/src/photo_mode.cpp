// HUD toggle and photo mode. The HUD key skips CHud::Draw and CHud::DrawAfterFade (radar, crosshair, money, help
// text, big messages). Photo mode hides the HUD, slows the world to a crawl (CTimer::ms_fTimeScale; the game keeps
// processing the camera, a real pause would stop it) and gives the camera to the mod the way script cameras work:
// SetCamPositionForFixedMode + TakeControlNoEntity every tick, Restore on exit. Only in free roam: missions and
// cutscenes own the camera.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cmath>
#include <memory>

namespace {

struct Vec {
    float x, y, z;
};

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
void(__cdecl* g_origDraw)() = nullptr;
void(__cdecl* g_origDrawAfterFade)() = nullptr;
bool g_hudHidden = false;
bool g_photo = false;
float g_savedTimeScale = 1.0f;
Vec g_pos{};
float g_yaw = 0.0f, g_pitch = 0.0f;
ULONGLONG g_lastTick = 0;

void __cdecl HookedDraw() {
    if (!g_hudHidden && !g_photo) {
        g_origDraw();
    }
}

void __cdecl HookedDrawAfterFade() {
    if (!g_hudHidden && !g_photo) {
        g_origDrawAfterFade();
    }
}

uint8_t* Camera() {
    return reinterpret_cast<uint8_t*>(g_host->Symbol("TheCamera"));
}

bool Held(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;  // answers "not pressed" while the game is in the background
}

void ApplyCamera() {
    const Vec forward{std::cos(g_pitch) * std::cos(g_yaw), std::cos(g_pitch) * std::sin(g_yaw), std::sin(g_pitch)};
    const Vec lookAt{g_pos.x + forward.x, g_pos.y + forward.y, g_pos.z + forward.z};
    const Vec up{0.0f, 0.0f, 0.0f};  // no extra up offset (the script command passes 0, 0, 0 as well)
    using SetFixed = void(__thiscall*)(void*, const Vec*, const Vec*);
    using TakeControl = void(__thiscall*)(void*, const Vec*, short, int);
    g_host->GameFunction<SetFixed>("CCamera::SetCamPositionForFixedMode")(Camera(), &g_pos, &up);
    g_host->GameFunction<TakeControl>("CCamera::TakeControlNoEntity")(Camera(), &lookAt, 2, 1);  // jump cut, script
}

void EnterPhoto() {
    if (!g_game->FreeRoam()) {
        g_game->ShowHelp("Photo mode is not available during missions and cutscenes.");
        return;
    }
    // Start where the game camera is: CPlaceable::m_matrix (+0x14), forward at +0x10, position at +0x30.
    const auto* matrix = *reinterpret_cast<const float* const*>(Camera() + 0x14);
    if (!matrix) {
        return;
    }
    g_pos = {matrix[12], matrix[13], matrix[14]};
    g_yaw = std::atan2(matrix[5], matrix[4]);
    g_pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, matrix[6])));
    auto* timeScale = g_host->GameVariable<float>("CTimer::ms_fTimeScale");
    g_savedTimeScale = *timeScale;
    if (g_host->ConfigBool("freeze_world", true)) {
        *timeScale = 0.0f;  // CTimer::UpdateVariables keeps a minimum step: the world crawls, the camera runs
    }
    g_photo = true;
    g_lastTick = GetTickCount64();
    ApplyCamera();
    g_host->Info("photo mode on at ({:.1f}, {:.1f}, {:.1f})", g_pos.x, g_pos.y, g_pos.z);
}

void LeavePhoto() {
    if (!g_photo) {
        return;
    }
    g_photo = false;
    *g_host->GameVariable<float>("CTimer::ms_fTimeScale") = g_savedTimeScale;
    g_host->GameFunction<void(__thiscall*)(void*)>("CCamera::Restore")(Camera());
    g_host->Info("photo mode off");
}

void Tick() {
    if (!g_photo) {
        return;
    }
    if (g_host->GameState() != 9 || g_game->OnMission() || g_game->CutsceneRunning()) {
        LeavePhoto();
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const float dt = std::fmin(0.1f, static_cast<float>(now - g_lastTick) / 1000.0f);  // real time: the world is slowed
    g_lastTick = now;
    const auto* mouse = reinterpret_cast<const float*>(g_host->Symbol("CPad::NewMouseControllerState"));
    const float sensitivity = static_cast<float>(g_host->ConfigDouble("mouse_sensitivity", 1.0)) * 0.004f;
    if (mouse) {
        g_yaw -= mouse[3] * sensitivity;    // X at +0x0C
        g_pitch -= mouse[4] * sensitivity;  // Y at +0x10
        g_pitch = std::fmax(-1.5f, std::fmin(1.5f, g_pitch));
    }
    float speed = static_cast<float>(g_host->ConfigDouble("camera_speed", 8.0)) * dt;
    if (Held(VK_SHIFT)) {
        speed *= 4.0f;
    }
    const Vec forward{std::cos(g_pitch) * std::cos(g_yaw), std::cos(g_pitch) * std::sin(g_yaw), std::sin(g_pitch)};
    const Vec right{std::sin(g_yaw), -std::cos(g_yaw), 0.0f};
    const float f = (Held('W') ? 1.0f : 0.0f) - (Held('S') ? 1.0f : 0.0f);
    const float r = (Held('D') ? 1.0f : 0.0f) - (Held('A') ? 1.0f : 0.0f);
    const float u = (Held('E') ? 1.0f : 0.0f) - (Held('Q') ? 1.0f : 0.0f);
    g_pos.x += (forward.x * f + right.x * r) * speed;
    g_pos.y += (forward.y * f + right.y * r) * speed;
    g_pos.z += (forward.z * f + u) * speed;
    ApplyCamera();
}

void BindKeys() {
    g_host->ClearKeys();
    if (!g_host->ConfigBool("enabled", true)) {
        return;
    }
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("hud_key", VK_F8)), [] {
        g_hudHidden = !g_hudHidden;
        g_host->Info("HUD {}", g_hudHidden ? "hidden" : "shown");
    });
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("photo_key", VK_F7)), [] {
        if (g_host->GameState() != 9) {
            return;
        }
        if (!g_host->Has("camera.free")) {
            g_hudHidden = !g_hudHidden;  // no free camera on this build: the photo key hides the HUD
            return;
        }
        g_photo ? LeavePhoto() : EnterPhoto();
    });
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("hud.toggle")) {
        g_host->Warn("HUD toggle is not available on this build");
        return 0;
    }
    using DrawFn = void __cdecl();
    if (!g_host->Hook<DrawFn>("hud_draw", "CHud::Draw", "hide the HUD", g_host->GameFunction<DrawFn*>("CHud::Draw"),
                              &HookedDraw, &g_origDraw) ||
        !g_host->Hook<DrawFn>("hud_draw_after_fade", "CHud::DrawAfterFade", "hide big messages in photos",
                              g_host->GameFunction<DrawFn*>("CHud::DrawAfterFade"), &HookedDrawAfterFade,
                              &g_origDrawAfterFade)) {
        g_host->Error("could not hook the HUD drawing");
        return 1;
    }
    BindKeys();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { BindKeys(); });
    if (g_host->Has("camera.free")) {
        g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
        g_host->On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent&) { LeavePhoto(); });
    } else {
        g_host->Warn("free camera is not available on this build: the photo key only hides the HUD");
    }
    g_host->Info("ready: HUD key {}, photo key {}", g_host->ConfigInt("hud_key", VK_F8),
                 g_host->ConfigInt("photo_key", VK_F7));
    return 0;
}
