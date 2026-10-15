// XInput controller. The Steam build calls XInputGetState
// in its per-pad update and supports Xbox-style controllers itself; 1.0 US only reads DirectInput joysticks. On
// 1.0 the mod runs after CPad::UpdatePads and merges the XInput state into pad 0's NewState (CControllerState:
// int16 sticks -128..128 and buttons 0/255, the console layout the game logic reads), keeping keyboard and mouse:
// a field is only taken from the controller when the controller is the more active source.
// Layout: A = Cross, X = Square, Y = Triangle, B = Circle, LB/RB = L1/R1, LT/RT = L2/R2, Back = Select, Start, the
// stick clicks, D-pad.
#include <sacore/sacore.hpp>

#include <windows.h>
#include <Xinput.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
void(__cdecl* g_origUpdatePads)() = nullptr;
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
GetStateFn g_getState = nullptr;
bool g_connectedLogged = false;

enum Field : int {
    LeftStickX = 0x00, LeftStickY = 0x02, RightStickX = 0x04, RightStickY = 0x06,
    L1 = 0x08, L2 = 0x0A, R1 = 0x0C, R2 = 0x0E,
    DPadUp = 0x10, DPadDown = 0x12, DPadLeft = 0x14, DPadRight = 0x16,
    Start = 0x18, Select = 0x1A, Square = 0x1C, Triangle = 0x1E, Cross = 0x20, Circle = 0x22,
    ShockL = 0x24, ShockR = 0x26,
};

int16_t Stick(SHORT value, float deadzone, bool invert) {
    float v = value / 32767.0f;
    if (std::fabs(v) < deadzone) {
        return 0;
    }
    v = (v - std::copysign(deadzone, v)) / (1.0f - deadzone);  // rescale outside the dead zone
    const auto scaled = static_cast<int16_t>(std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 128.0f));
    return invert ? static_cast<int16_t>(-scaled) : scaled;
}

void Merge(uint8_t* state, int field, int16_t value) {
    auto& current = *reinterpret_cast<int16_t*>(state + field);
    if (std::abs(value) > std::abs(current)) {
        current = value;
    }
}

void __cdecl HookedUpdatePads() {
    g_origUpdatePads();
    if (!g_getState || !g_host->ConfigBool("enabled", true)) {
        return;
    }
    XINPUT_STATE xs{};
    if (g_getState(static_cast<DWORD>(g_host->ConfigInt("controller", 0)), &xs) != ERROR_SUCCESS) {
        g_connectedLogged = false;
        return;
    }
    if (!g_connectedLogged) {
        g_connectedLogged = true;
        g_host->Info("controller {} connected", g_host->ConfigInt("controller", 0));
    }
    auto* pad = reinterpret_cast<uint8_t*>(g_host->Symbol("CPad::Pads"));  // pad 0, NewState at +0
    const auto dz = static_cast<float>(g_host->ConfigDouble("deadzone", 0.2));
    const XINPUT_GAMEPAD& g = xs.Gamepad;
    // The console layout has "up" negative on the sticks.
    Merge(pad, LeftStickX, Stick(g.sThumbLX, dz, false));
    Merge(pad, LeftStickY, Stick(g.sThumbLY, dz, true));
    Merge(pad, RightStickX, Stick(g.sThumbRX, dz, false));
    Merge(pad, RightStickY, Stick(g.sThumbRY, dz, true));
    auto button = [&](int field, bool down) { Merge(pad, field, down ? 255 : 0); };
    button(Cross, g.wButtons & XINPUT_GAMEPAD_A);
    button(Square, g.wButtons & XINPUT_GAMEPAD_X);
    button(Triangle, g.wButtons & XINPUT_GAMEPAD_Y);
    button(Circle, g.wButtons & XINPUT_GAMEPAD_B);
    button(L1, g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
    button(R1, g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    button(L2, g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    button(R2, g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    button(DPadUp, g.wButtons & XINPUT_GAMEPAD_DPAD_UP);
    button(DPadDown, g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
    button(DPadLeft, g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
    button(DPadRight, g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    button(Start, g.wButtons & XINPUT_GAMEPAD_START);
    button(Select, g.wButtons & XINPUT_GAMEPAD_BACK);
    button(ShockL, g.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
    button(ShockR, g.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (g_host->GameBuild() != "us10_hoodlum") {
        g_host->Info("this build reads XInput controllers itself: nothing to do");
        return 0;
    }
    if (!g_host->Has("input.pads")) {
        g_host->Warn("pad access is not available on this build");
        return 0;
    }
    for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
        if (HMODULE module = LoadLibraryA(dll)) {
            g_getState = reinterpret_cast<GetStateFn>(GetProcAddress(module, "XInputGetState"));
            if (g_getState) {
                g_host->Info("using {}", dll);
                break;
            }
        }
    }
    if (!g_getState) {
        g_host->Warn("no XInput DLL found on this system");
        return 0;
    }
    using UpdateFn = void __cdecl();
    if (!g_host->Hook<UpdateFn>("update_pads", "CPad::UpdatePads", "merge the XInput controller into pad 0",
                                g_host->GameFunction<UpdateFn*>("CPad::UpdatePads"), &HookedUpdatePads,
                                &g_origUpdatePads)) {
        g_host->Error("could not hook CPad::UpdatePads");
        return 1;
    }
    g_host->Info("ready: controller {}", g_host->ConfigInt("controller", 0));
    return 0;
}
