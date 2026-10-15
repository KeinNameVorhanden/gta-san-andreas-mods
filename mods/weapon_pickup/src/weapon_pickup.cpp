// Weapon pickup by key. The game already has a "collect by key"
// window: CPickups::Update opens it for 6 frames when the pickup control is pressed. It only gates swapping a weapon
// in an occupied slot; empty slots fill automatically through CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment.
// The hook answers "no" to that question unless the window is open (or the extra key is held), so new weapons and
// swaps both need the key. Ammo for weapons CJ already carries, health, armour and money pickups are unchanged.
#include <sacore/sacore.hpp>

#include <windows.h>

#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
bool(__cdecl* g_origCanPickUp)(int) = nullptr;

bool __cdecl HookedCanPickUp(int weaponType) {
    if (g_host->ConfigBool("enabled", true)) {
        const int* window = g_host->GameVariable<int>("CPickups::CollectPickupWindow");
        const auto key = static_cast<int>(g_host->ConfigInt("pickup_key", 0));
        const bool keyHeld = key > 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
        if (window && *window == 0 && !keyHeld) {
            return false;
        }
    }
    return g_origCanPickUp(weaponType);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->Has("pickups.weapon_key")) {
        g_host->Warn("weapon pickup by key is not available on this build");
        return 0;
    }
    using CanFn = bool __cdecl(int);
    if (!g_host->Hook<CanFn>("can_pick_up", "CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment",
                             "weapons only by key",
                             g_host->GameFunction<CanFn*>("CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment"),
                             &HookedCanPickUp, &g_origCanPickUp)) {
        g_host->Error("could not hook the weapon pickup check");
        return 1;
    }
    g_host->Info("ready: weapons only with the pickup control{}",
                 g_host->ConfigInt("pickup_key", 0) > 0 ? " or the extra key" : "");
    return 0;
}
