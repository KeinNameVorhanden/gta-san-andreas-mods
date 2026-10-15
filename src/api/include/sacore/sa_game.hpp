// Header-only helpers for gameplay mods: game state the QoL mods share, read through SACore's adapter symbols.
// BUILD-SPECIFIC underneath (docs/ADDRESSES.md): every helper checks its capability and returns a
// safe default when the build does not support it. Call from the game thread only.
#pragma once

#include <sacore/sacore.hpp>

#include <cstdint>

namespace sacore::sa {

class Game {
public:
    explicit Game(const mod::Host& host) : host_(host) {}

    // Story or side mission running ($ONMISSION in the main script).
    bool OnMission() const {
        if (!host_.Has("game.mission.read")) {
            return false;
        }
        const int offset = *host_.GameVariable<int>("CTheScripts::OnAMissionFlag");
        return offset != 0 && *reinterpret_cast<const int*>(host_.Symbol("CTheScripts::ScriptSpace") + offset) == 1;
    }
    bool MiniGame() const {
        return host_.Has("game.mission.read") && *host_.GameVariable<uint8_t>("CTheScripts::bMiniGameInProgress") != 0;
    }
    bool CutsceneRunning() const {
        return host_.Has("game.mission.read") && *host_.GameVariable<uint8_t>("CCutsceneMgr::ms_running") != 0;
    }
    // Scripted scenes take the controls away (CPad::DisablePlayerControls, pad 0).
    bool ControlsDisabled() const {
        if (!host_.Has("game.mission.read")) {
            return false;
        }
        const auto pad = host_.GameFunction<uint8_t*(__cdecl*)(int)>("CPad::GetPad")(0);
        return pad && *reinterpret_cast<const uint16_t*>(pad + 0x10E) != 0;
    }
    // 0 playing, 1 wasted, 2 busted (CPlayerInfo::m_nPlayerState).
    int PlayerState() const {
        const uintptr_t players = host_.Symbol("CWorld::Players");
        return players ? *reinterpret_cast<const uint8_t*>(players + 0xDC) : -1;
    }
    uint8_t* PlayerPed() const {
        const uintptr_t players = host_.Symbol("CWorld::Players");
        return players ? *reinterpret_cast<uint8_t* const*>(players) : nullptr;
    }
    bool InVehicle() const {
        const uint8_t* ped = PlayerPed();
        return ped && (*reinterpret_cast<const uint32_t*>(ped + 0x46C) & 0x100) != 0;
    }
    int WantedLevel() const {
        const uint8_t* ped = PlayerPed();
        const auto* data = ped ? *reinterpret_cast<uint8_t* const*>(ped + 0x480) : nullptr;
        const auto* wanted = data ? *reinterpret_cast<uint8_t* const*>(data) : nullptr;
        return wanted ? *reinterpret_cast<const int*>(wanted + 0x2C) : 0;
    }
    // Free roam: playing, alive, no mission, mini game, cutscene or scripted scene.
    bool FreeRoam() const {
        return host_.GameState() == 9 && PlayerState() == 0 && !OnMission() && !MiniGame() && !CutsceneRunning() &&
               !ControlsDisabled();
    }
    // Int stats (id >= 120), for example 147 = missions passed.
    int StatInt(int id) const {
        return host_.Has("stats.read") ? host_.GameVariable<int>("CStats::StatTypesInt")[id - 120] : 0;
    }

    // Shows text in the game's help box (top left), like the game's own hints. ASCII only.
    void ShowHelp(const char* text) const {
        if (host_.Has("hud.help_message")) {
            host_.GameFunction<void(__cdecl*)(const char*, bool, bool, bool)>("CHud::SetHelpMessage")(text, false,
                                                                                                       false, false);
        }
    }

private:
    const mod::Host& host_;
};

}  // namespace sacore::sa
