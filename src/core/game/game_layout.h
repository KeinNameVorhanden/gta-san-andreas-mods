// Struct field offsets used by the core. Identical on both supported builds (runtime verified, see
// docs/ADDRESSES.md); taken from plugin-sdk's 1.0 US layouts.
#pragma once

#include <cstdint>

namespace sacore::layout {

// CPlayerInfo (CWorld::Players[], stride 0x190)
inline constexpr uintptr_t kPlayerInfoPed = 0x00;
inline constexpr uintptr_t kPlayerInfoMoney = 0xB8;

// CEntity / CPhysical / CPed
inline constexpr uintptr_t kPlacementPos = 0x04;
inline constexpr uintptr_t kEntityMatrix = 0x14;
inline constexpr uintptr_t kEntityArea = 0x2F;
inline constexpr uintptr_t kMatrixPos = 0x30;
inline constexpr uintptr_t kPedPlayerData = 0x480;
inline constexpr uintptr_t kPedHealth = 0x540;
inline constexpr uintptr_t kPedArmour = 0x548;
inline constexpr uintptr_t kWantedLevel = 0x2C;  // CWanted*, read from CPlayerData+0

// Main loop states (WinMain switch on gGameState, both builds).
enum GameState : int {
    kStartUp = 0,
    kInitLogoMovie = 1,
    kLogoMovie = 2,
    kInitIntroMovie = 3,
    kIntroMovie = 4,
    kInitOnce = 5,
    kInitFrontend = 6,
    kFrontend = 7,
    kInitPlayingGame = 8,
    kPlayingGame = 9,
};

}  // namespace sacore::layout
