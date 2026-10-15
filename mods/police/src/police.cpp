// Police switches:
// - never wanted: CCheat::m_aCheatsActive[65] (I DO AS I PLEASE), which CWanted::SetWantedLevel and three other
//   places check;
// - wanted cap and faster decay: CWanted::SetWantedLevel on the player's CWanted (the story's own cap, set by
//   scripts, stays untouched);
// - heli shots: the four interval constants for 3 to 6 stars in CHeli::ProcessControl become 999999 ms (the value the
//   game uses for 0 to 2 stars), written through the core patch manager and restored when switched off;
// - no helis: CHeli::bPoliceHelisAllowed = false (scripts switch it back on, so it is re-asserted every tick).
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr int kNeverWantedCheat = 65;
// Offsets of the imm32 operands from the interval switch (`cmp eax, 6; ja; jmp [table]` ...), levels 3 to 6, and
// the values the game ships with.
constexpr int kIntervalOffsets[] = {0x14, 0x1B, 0x22, 0x29};
constexpr uint32_t kIntervalValues[] = {10000, 5000, 3500, 2000};
constexpr uint32_t kNever = 999999;

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
bool g_cheatSetByUs = false;
bool g_heliPatched = false;
bool g_helisOffByUs = false;
ULONGLONG g_lastDecay = 0;

uint8_t* PlayerWanted() {
    uint8_t* ped = g_game->PlayerPed();
    auto* data = ped ? *reinterpret_cast<uint8_t**>(ped + 0x480) : nullptr;
    return data ? *reinterpret_cast<uint8_t**>(data) : nullptr;
}

void SetWanted(uint8_t* wanted, int level) {
    g_host->GameFunction<void(__thiscall*)(void*, int)>("CWanted::SetWantedLevel")(wanted, level);
}

void SetHeliShots(bool off) {
    if (off == g_heliPatched || !g_host->HasPatching()) {
        return;
    }
    const uintptr_t site = g_host->Symbol("CHeli::ProcessControl police shot interval");
    if (!site) {
        return;
    }
    if (!off) {
        for (int i = 0; i < 4; ++i) {
            g_host->RemovePatch(("heli_interval_" + std::to_string(i)).c_str());
        }
        g_heliPatched = false;
        g_host->Info("police helicopter shots restored");
        return;
    }
    for (int i = 0; i < 4; ++i) {
        uint32_t current = 0;
        std::memcpy(&current, reinterpret_cast<const void*>(site + kIntervalOffsets[i]), 4);
        if (current != kIntervalValues[i]) {
            g_host->Warn("heli interval {} is {} instead of {}: not patching", i + 3, current, kIntervalValues[i]);
            return;
        }
    }
    for (int i = 0; i < 4; ++i) {
        g_host->WriteMemory(("heli_interval_" + std::to_string(i)).c_str(), "CHeli::ProcessControl shot interval",
                            "police helicopters do not shoot", site + kIntervalOffsets[i], &kNever, 4);
    }
    g_heliPatched = true;
    g_host->Info("police helicopter shots off");
}

void Tick() {
    const bool mission = g_host->ConfigBool("respect_missions", true) && g_game->OnMission();
    const bool on = g_host->ConfigBool("enabled", true) && !mission;
    // Never wanted.
    if (g_host->Has("police.wanted")) {
        bool* cheat = g_host->GameVariable<bool>("CCheat::m_aCheatsActive") + kNeverWantedCheat;
        const bool want = on && g_host->ConfigBool("never_wanted", false);
        if (want && !*cheat) {
            *cheat = true;
            g_cheatSetByUs = true;
        } else if (!want && g_cheatSetByUs) {
            *cheat = false;
            g_cheatSetByUs = false;
        }
        if (want) {
            if (uint8_t* wanted = PlayerWanted(); wanted && *reinterpret_cast<int*>(wanted + 0x2C) > 0) {
                // The cheat blocks new stars only; clear the ones CJ had. SetWantedLevel returns early while the
                // flag is set, so lift it for the call.
                *cheat = false;
                SetWanted(wanted, 0);
                *cheat = true;
            }
        }
    }
    // Cap and decay.
    uint8_t* wanted = PlayerWanted();
    if (on && wanted && g_host->Has("police.wanted")) {
        const int level = *reinterpret_cast<int*>(wanted + 0x2C);
        const int cap = static_cast<int>(g_host->ConfigInt("max_wanted", 6));
        if (level > cap) {
            SetWanted(wanted, cap);
            g_host->Info("wanted level {} capped to {}", level, cap);
        } else if (level > 0 && g_host->ConfigBool("faster_decay", false)) {
            const ULONGLONG now = GetTickCount64();
            const auto player = g_host->Player();
            using Presence = int(__cdecl*)(float, float, float, float);  // CVector by value, radius
            const bool policeNear =
                player && g_host->GameFunction<Presence>("CWanted::WorkOutPolicePresence")(
                              player->position[0], player->position[1], player->position[2], 18.0f) > 0;
            if (policeNear || g_lastDecay == 0) {
                g_lastDecay = now;
            } else if (now - g_lastDecay > static_cast<ULONGLONG>(g_host->ConfigInt("decay_seconds", 30)) * 1000) {
                SetWanted(wanted, level - 1);
                g_lastDecay = now;
                g_host->Info("wanted level {} -> {} (no police near)", level, level - 1);
            }
        } else {
            g_lastDecay = 0;
        }
    }
    // Helicopters.
    if (g_host->Has("police.heli")) {
        SetHeliShots(on && g_host->ConfigBool("no_heli_shots", false));
        bool* allowed = g_host->GameVariable<bool>("CHeli::bPoliceHelisAllowed");
        if (on && g_host->ConfigBool("no_police_helis", false)) {
            *allowed = false;
            g_helisOffByUs = true;
        } else if (g_helisOffByUs) {
            *allowed = true;
            g_helisOffByUs = false;
        }
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("police.wanted") && !g_host->Has("police.heli")) {
        g_host->Warn("police switches are not available on this build");
        return 0;
    }
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->Info("ready: never wanted {}, max {} stars, faster decay {}, heli shots off {}, helis off {}",
                 g_host->ConfigBool("never_wanted", false), g_host->ConfigInt("max_wanted", 6),
                 g_host->ConfigBool("faster_decay", false), g_host->ConfigBool("no_heli_shots", false),
                 g_host->ConfigBool("no_police_helis", false));
    return 0;
}
