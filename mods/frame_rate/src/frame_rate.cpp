// Frame rate. With the menu's Frame Limiter on, WinMain
// skips frames until 1000 / RsGlobal.frameLimit ms have passed (default 30); the mod writes the chosen limit
// (up to 60: above that more subsystems misbehave, and the game's own busy-wait caps 1.0 at about 71 FPS).
// Like FramerateVigilante's AutoLimitFPS, the pool and girlfriend-date scripts (POOL2, GFSEX) get 30 FPS.
// High frame rate fix: the on-rails wheel spin adds a per-frame amount (fadd [esi+0x828..0x834]); the four sites are
// redirected to a stub that scales it by CTimer::ms_fTimeStep, as FramerateVigilante does (MIT, THIRD_PARTY.md).
// The physics and timing fixes for frame rates above 30 live in high_fps.cpp.
#include "high_fps.h"

#include <sacore/sacore.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
float* g_frTimeStep = nullptr;  // CTimer::ms_fTimeStep, read by the stubs
}

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
bool g_spinPatched = false;

// st(0) = spin amount * time step + [esi + offset], replacing `fadd dword ptr [esi + offset]`.
#define SPIN_STUB(name, offset)                    \
    __declspec(naked) void name() {                \
        __asm push eax                             \
        __asm mov eax, dword ptr [g_frTimeStep]    \
        __asm fmul dword ptr [eax]                 \
        __asm pop eax                              \
        __asm fadd dword ptr [esi + offset]        \
        __asm ret                                  \
    }
SPIN_STUB(Spin0, 0x828)
SPIN_STUB(Spin1, 0x82C)
SPIN_STUB(Spin2, 0x830)
SPIN_STUB(Spin3, 0x834)
#undef SPIN_STUB

void (*const kStubs[])() = {&Spin0, &Spin1, &Spin2, &Spin3};

void SetSpinFix(bool on) {
    if (on == g_spinPatched || !g_host->HasPatching()) {
        return;
    }
    if (!on) {
        for (int i = 0; i < 4; ++i) {
            g_host->RemovePatch(("wheel_spin_" + std::to_string(i)).c_str());
        }
        g_spinPatched = false;
        return;
    }
    uintptr_t sites[4];
    for (int i = 0; i < 4; ++i) {
        sites[i] = g_host->Symbol(("Wheel spin add " + std::to_string(i)).c_str());
        if (!sites[i]) {
            return;
        }
    }
    for (int i = 0; i < 4; ++i) {
        // call stub; nop (6 bytes, same length as the fadd)
        uint8_t code[6] = {0xE8, 0, 0, 0, 0, 0x90};
        const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(kStubs[i]) - (sites[i] + 5));
        std::memcpy(code + 1, &rel, 4);
        g_host->WriteMemory(("wheel_spin_" + std::to_string(i)).c_str(), "on-rails wheel spin",
                            "wheel spin per time step (high frame rates)", sites[i], code, sizeof(code));
    }
    g_spinPatched = true;
}

// Scripts whose own per-frame logic breaks above 30 FPS (FramerateVigilante's AutoLimitFPS list, issues and pull
// requests): pool, girlfriend dates, the gyms (exit loop), Fender Ketchup (cannot leave the
// garage). DRUGS1 inside the crack den gets 50 (Big Smoke stops walking above that).
int ScriptCap() {
    struct Cap {
        const char* name;
        int fps;
        bool interiorOnly;
    };
    static constexpr Cap kCaps[] = {
        {"POOL2", 30, false}, {"GFSEX", 30, false}, {"GYMLS", 30, false}, {"GYMSF", 30, false},
        {"GYMLV", 30, false}, {"CASINO1", 30, false}, {"DRUGS1", 50, true},
    };
    const int* area = g_host->GameVariable<int>("CGame::currArea");
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    int cap = 0;
    int guard = 0;
    for (uint8_t* script = head ? *head : nullptr; script && guard++ < 512; script = *reinterpret_cast<uint8_t**>(script)) {
        const char* name = reinterpret_cast<const char*>(script + 0x08);
        for (const Cap& c : kCaps) {
            if (_strnicmp(name, c.name, 8) == 0 && (!c.interiorOnly || (area && *area != 0)) && (cap == 0 || c.fps < cap)) {
                cap = c.fps;
            }
        }
    }
    return cap;
}

int g_lastWritten = 0;

void Apply() {
    auto* limit = g_host->GameVariable<int>("RsGlobal.frameLimit");
    if (!limit || !g_host->ConfigBool("enabled", true)) {
        return;
    }
    int fps = static_cast<int>(g_host->ConfigInt("fps", 30));
    if (g_host->ConfigBool("mini_games_at_30", true) && g_host->GameState() == 9) {
        if (const int cap = ScriptCap(); cap > 0 && cap < fps) {
            fps = cap;
        }
    }
    if (*limit != fps) {
        *limit = fps;
        if (fps != g_lastWritten) {
            g_host->Info("frame limit {} FPS", fps);
        }
        g_lastWritten = fps;
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->Has("engine.frame_limit")) {
        g_host->Warn("frame limit is not available on this build");
        return 0;
    }
    g_frTimeStep = g_host->GameVariable<float>("CTimer::ms_fTimeStep");
    SetSpinFix(g_frTimeStep && g_host->ConfigBool("enabled", true) && g_host->ConfigBool("wheel_spin_fix", true));
    g_host->On(SACORE_EVENT_FRAME, [](const SacoreEvent&) { Apply(); });
    frame_rate::InitHighFps(*g_host);
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) {
        SetSpinFix(g_frTimeStep && g_host->ConfigBool("enabled", true) && g_host->ConfigBool("wheel_spin_fix", true));
        frame_rate::SetHighFps(g_host->ConfigBool("enabled", true) && g_host->ConfigBool("physics_fixes", true));
    });
    g_host->Info("ready: {} FPS, wheel spin fix {}", g_host->ConfigInt("fps", 30), g_spinPatched);
    return 0;
}
