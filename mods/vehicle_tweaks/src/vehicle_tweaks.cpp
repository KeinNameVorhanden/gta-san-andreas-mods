// Vehicle tweaks:
// - no upside-down damage: CAutomobile::VehicleDamage skips the roof damage when bDoesNotGetDamagedUpsideDown
//   (bit 3 of +0x868) is set: `test byte [esi+0x868], 8; jne skip`. The `jne` becomes `nop; jmp` (same rel32), so
//   no car takes roof damage. Written through the core patch manager, restored when switched off.
// - mouse steering recenter: with mouse steering the game leaves the wheels where the mouse put them. Before
//   CAutomobile::ProcessControlInputs the raw steer angle (+0x58C) decays toward 0 while the mouse rests.
#include <sacore/sacore.hpp>

#include <cstdint>
#include <cstring>
#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
void(__fastcall* g_origControl)(void*, void*, uint8_t) = nullptr;
bool g_gatePatched = false;

void __fastcall HookedControl(void* car, void* edx, uint8_t pad) {
    const double rate = g_host->ConfigDouble("mouse_steering_recenter", 0.15);
    const auto* mouseSteering = g_host->GameVariable<bool>("CVehicle::m_bEnableMouseSteering");
    const auto* mouse = reinterpret_cast<const float*>(g_host->Symbol("CPad::NewMouseControllerState"));
    if (car && rate > 0.0 && g_host->ConfigBool("enabled", true) && mouseSteering && *mouseSteering && mouse &&
        mouse[3] == 0.0f) {
        auto& raw = *reinterpret_cast<float*>(static_cast<uint8_t*>(car) + 0x58C);
        raw *= static_cast<float>(1.0 - rate);
    }
    g_origControl(car, edx, pad);
}

void UpdateGate() {
    const bool want = g_host->ConfigBool("enabled", true) && g_host->ConfigBool("no_upside_down_damage", true);
    if (want == g_gatePatched || !g_host->HasPatching()) {
        return;
    }
    const uintptr_t gate = g_host->Symbol("CAutomobile::VehicleDamage upside-down gate");
    if (!gate) {
        return;
    }
    if (!want) {
        g_host->RemovePatch("upside_down_gate");
        g_gatePatched = false;
        return;
    }
    static constexpr uint8_t kJne[] = {0x0F, 0x85};
    static constexpr uint8_t kJmp[] = {0x90, 0xE9};  // nop; jmp rel32 (the rel32 stays: same end address)
    if (std::memcmp(reinterpret_cast<const void*>(gate + 7), kJne, 2) != 0) {
        g_host->Warn("roof damage check has unexpected bytes: not patching");
        return;
    }
    g_gatePatched = g_host->WriteMemory("upside_down_gate", "CAutomobile::VehicleDamage", "no damage on the roof",
                                        gate + 7, kJmp, 2);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (g_host->Has("vehicle.upside_down")) {
        UpdateGate();
    }
    if (g_host->Has("vehicle.steering") && g_host->Symbol("CVehicle::m_bEnableMouseSteering") &&
        g_host->Symbol("CPad::NewMouseControllerState")) {
        // __thiscall(CAutomobile*, uint8 pad), hooked as __fastcall with the unused edx.
        using ControlFn = void __fastcall(void*, void*, uint8_t);
        if (!g_host->Hook<ControlFn>("control_inputs", "CAutomobile::ProcessControlInputs",
                                     "steering returns to centre with mouse steering",
                                     g_host->GameFunction<ControlFn*>("CAutomobile::ProcessControlInputs"),
                                     &HookedControl, &g_origControl)) {
            g_host->Warn("steering hook not installed");
        }
    }
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) {
        if (g_host->Has("vehicle.upside_down")) {
            UpdateGate();
        }
    });
    g_host->Info("ready: no roof damage {}, mouse steering recenter {}", g_gatePatched,
                 g_host->ConfigDouble("mouse_steering_recenter", 0.15));
    return 0;
}
