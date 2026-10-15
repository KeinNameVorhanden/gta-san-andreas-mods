// Mouse and aim. Every mouse camera mode reads CPad::NewMouseControllerState.X/Y (+0x0C/+0x10) after
// CPad::UpdateMouse filled it. The hook runs after UpdateMouse and
// scales the frame's delta: aim sensitivity while the aim button is held, look sensitivity otherwise, optional
// per-axis invert. Raw input: WM_INPUT mouse deltas from Windows replace the game's DirectInput delta; the sign
// convention of each axis is learned from frames where both moved, so it matches the game on both builds.
#include <sacore/sacore.hpp>

#include <windows.h>

#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
void(__fastcall* g_origUpdateMouse)(void*, void*) = nullptr;
long g_rawX = 0, g_rawY = 0;    // accumulated since the last UpdateMouse
float g_signX = 1.0f, g_signY = 1.0f;
bool g_rawRegistered = false;

void __fastcall HookedUpdateMouse(void* pad, void* edx) {
    g_origUpdateMouse(pad, edx);
    auto* state = reinterpret_cast<float*>(g_host->Symbol("CPad::NewMouseControllerState"));
    const long rawX = g_rawX, rawY = g_rawY;
    g_rawX = g_rawY = 0;
    if (!state || !g_host->ConfigBool("enabled", true) || g_host->GameState() != 9) {
        return;
    }
    float& x = state[3];
    float& y = state[4];
    if (g_host->ConfigBool("raw_input", false) && g_rawRegistered) {
        if (x != 0.0f && rawX != 0) {
            g_signX = (x > 0.0f) == (rawX > 0) ? 1.0f : -1.0f;
        }
        if (y != 0.0f && rawY != 0) {
            g_signY = (y > 0.0f) == (rawY > 0) ? 1.0f : -1.0f;
        }
        x = g_signX * static_cast<float>(rawX);
        y = g_signY * static_cast<float>(rawY);
    }
    const auto aimButton = static_cast<int>(g_host->ConfigInt("aim_button", VK_RBUTTON));
    const bool aiming = (GetAsyncKeyState(aimButton) & 0x8000) != 0;
    const auto factor = static_cast<float>(
        g_host->ConfigDouble(aiming ? "aim_sensitivity" : "look_sensitivity", 1.0));
    x *= factor * (g_host->ConfigBool("invert_x", false) ? -1.0f : 1.0f);
    y *= factor * (g_host->ConfigBool("invert_y", false) ? -1.0f : 1.0f);
}

int RawInputFilter(void*, uint32_t message, uintptr_t, intptr_t lparam, intptr_t*, void*) {
    if (message != WM_INPUT) {
        return 0;
    }
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) !=
            static_cast<UINT>(-1) &&
        input.header.dwType == RIM_TYPEMOUSE && !(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        g_rawX += input.data.mouse.lLastX;
        g_rawY += input.data.mouse.lLastY;
    }
    return 0;  // the game still gets the message
}

void RegisterRaw() {
    if (g_rawRegistered || !g_host->ConfigBool("raw_input", false)) {
        return;
    }
    auto* window = static_cast<HWND>(g_host->Raw()->game_window(g_host->Raw()));
    if (!window) {
        return;
    }
    RAWINPUTDEVICE device{0x01, 0x02, 0, window};  // generic desktop, mouse; no RIDEV_NOLEGACY: DirectInput keeps working
    g_rawRegistered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
    g_host->Info("raw mouse input {}", g_rawRegistered ? "registered" : "could not be registered");
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->Symbol("CPad::UpdateMouse") || !g_host->Symbol("CPad::NewMouseControllerState")) {
        g_host->Warn("mouse settings are not available on this build");
        return 0;
    }
    using UpdateFn = void __fastcall(void*, void*);  // __thiscall(CPad*), no stack arguments
    if (!g_host->Hook<UpdateFn>("update_mouse", "CPad::UpdateMouse", "aim sensitivity, invert, raw input",
                                g_host->GameFunction<UpdateFn*>("CPad::UpdateMouse"), &HookedUpdateMouse,
                                &g_origUpdateMouse)) {
        g_host->Error("could not hook the mouse update");
        return 1;
    }
    raw->add_message_filter(raw, &RawInputFilter, nullptr);
    RegisterRaw();
    g_host->On(SACORE_EVENT_DEVICE_CREATED, [](const SacoreEvent&) { RegisterRaw(); });
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { RegisterRaw(); });
    g_host->Info("ready: look x{}, aim x{}, invert x {} y {}, raw {}", g_host->ConfigDouble("look_sensitivity", 1.0),
                 g_host->ConfigDouble("aim_sensitivity", 1.0), g_host->ConfigBool("invert_x", false),
                 g_host->ConfigBool("invert_y", false), g_host->ConfigBool("raw_input", false));
    return 0;
}
