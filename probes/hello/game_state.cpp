// Change-only logging of game state on the game thread, plus the test-only
// "start a new game from the main menu" trigger (patch P2, docs/ADDRESSES.md).

#include "probe.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>

namespace probe {
namespace {

// CMenuManager: the WinMain frontend case reads these two flags on both builds.
constexpr uintptr_t kMenuActiveOffset = 0x5C;
constexpr uintptr_t kStartGameLoadingOffset = 0x60;

// CPlayerInfo (stride 0x190 and money at +0xB8 confirmed in the code of both builds).
constexpr uintptr_t kPlayerInfoMoneyOffset = 0xB8;

// CEntity / CPhysical / CPed layout from plugin-sdk (1.0 US); checked at runtime on each build.
constexpr uintptr_t kPlacementPosOffset = 0x04;
constexpr uintptr_t kEntityMatrixOffset = 0x14;
constexpr uintptr_t kEntityAreaOffset = 0x2F;
constexpr uintptr_t kMatrixPosOffset = 0x30;
constexpr uintptr_t kPedPlayerDataOffset = 0x480;
constexpr uintptr_t kPedHealthOffset = 0x540;
constexpr uintptr_t kPedArmorOffset = 0x548;
constexpr uintptr_t kWantedLevelOffset = 0x2C;  // CWanted::m_nWantedLevel, CPlayerData+0 holds CWanted*

constexpr int kGameStateFrontend = 7;
constexpr int kAutoStartDelayTicks = 6;         // samples in the frontend before triggering
constexpr ULONGLONG kSampleIntervalMs = 500;
constexpr float kPositionLogDistance = 100.0f;  // log position when the player moved this far

struct Addresses {
    uintptr_t gameState = 0;
    uintptr_t menuActive = 0;
    uintptr_t startGameLoading = 0;
    uintptr_t players = 0;
    uintptr_t currArea = 0;
};

struct Snapshot {
    int gameState = -1;
    int menuActive = -1;
    int currArea = -1;
    uintptr_t ped = 0;
    int pedArea = -1;
    int health = -1;  // rounded
    int armor = -1;
    int money = INT_MIN;
    int wanted = -1;

    bool operator==(const Snapshot&) const = default;
};

Addresses g_addresses;
bool g_enabled = false;
bool g_autoStart = false;
bool g_autoStartDone = false;
int g_frontendTicks = 0;
ULONGLONG g_lastSample = 0;
Snapshot g_last;
float g_lastPos[3] = {0, 0, 0};

uintptr_t Resolve(uintptr_t rva, const char* name, size_t minReferences) {
    if (rva == 0) {
        Logf("state: {} not mapped for this build", name);
        return 0;
    }
    const uintptr_t address = ModuleBase() + rva;
    const size_t references = CountCodeReferences(address);
    const bool ok = references >= minReferences;
    Logf("state: {} at {:#010x}: {} code references (need {}) -> {}", name, address, references, minReferences,
         ok ? "ok" : "DISABLED");
    return ok ? address : 0;
}

template <class T>
T ReadOr(uintptr_t address, T fallback) {
    T value{};
    return address && SafeRead(address, value) ? value : fallback;
}

bool ReadPedPosition(uintptr_t ped, float (&pos)[3]) {
    const auto matrix = ReadOr<uintptr_t>(ped + kEntityMatrixOffset, 0);
    const uintptr_t source = matrix ? matrix + kMatrixPosOffset : ped + kPlacementPosOffset;
    return SafeRead(source, pos);
}

Snapshot Sample(float (&pos)[3], bool& havePos) {
    Snapshot s;
    s.gameState = ReadOr<int>(g_addresses.gameState, -1);
    s.menuActive = ReadOr<unsigned char>(g_addresses.menuActive, 0xFF);
    s.currArea = ReadOr<int>(g_addresses.currArea, -1);
    s.ped = ReadOr<uintptr_t>(g_addresses.players, 0);
    s.money = ReadOr<int>(g_addresses.players ? g_addresses.players + kPlayerInfoMoneyOffset : 0, INT_MIN);
    havePos = false;
    if (s.ped) {
        s.pedArea = ReadOr<unsigned char>(s.ped + kEntityAreaOffset, 0xFF);
        s.health = static_cast<int>(std::lround(ReadOr<float>(s.ped + kPedHealthOffset, -1.0f)));
        s.armor = static_cast<int>(std::lround(ReadOr<float>(s.ped + kPedArmorOffset, -1.0f)));
        const auto playerData = ReadOr<uintptr_t>(s.ped + kPedPlayerDataOffset, 0);
        const auto wanted = playerData ? ReadOr<uintptr_t>(playerData, 0) : 0;
        s.wanted = wanted ? ReadOr<int>(wanted + kWantedLevelOffset, -1) : -1;
        havePos = ReadPedPosition(s.ped, pos);
    }
    return s;
}

void MaybeAutoStart(const Snapshot& s) {
    if (!g_autoStart || g_autoStartDone || !g_addresses.startGameLoading) {
        return;
    }
    g_frontendTicks = s.gameState == kGameStateFrontend ? g_frontendTicks + 1 : 0;
    if (g_frontendTicks >= kAutoStartDelayTicks) {
        *reinterpret_cast<volatile unsigned char*>(g_addresses.startGameLoading) = 1;
        g_autoStartDone = true;
        Logf("P2 auto start: set FrontEndMenuManager start-game-loading flag at {:#010x}", g_addresses.startGameLoading);
    }
}

}  // namespace

void GameStateInit(const KnownBuild& build, bool autoStartNewGame) {
    g_addresses.gameState = Resolve(build.gameStateRva, "gGameState", 3);
    const uintptr_t menu = build.menuManagerRva;
    g_addresses.menuActive = Resolve(menu ? menu + kMenuActiveOffset : 0, "FrontEndMenuManager.m_bMenuActive", 3);
    g_addresses.startGameLoading =
        Resolve(menu ? menu + kStartGameLoadingOffset : 0, "FrontEndMenuManager.m_bStartGameLoading", 1);
    g_addresses.players = Resolve(build.playersRva, "CWorld::Players", 10);
    g_addresses.currArea = Resolve(build.currAreaRva, "CGame::currArea", 3);
    g_enabled = g_addresses.gameState != 0;
    g_autoStart = autoStartNewGame;
    Logf("state: logging {}, auto start new game {}", g_enabled ? "on" : "off", g_autoStart ? "on (test only)" : "off");
}

void GameStateTick() {
    if (!g_enabled) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastSample < kSampleIntervalMs) {
        return;
    }
    g_lastSample = now;

    float pos[3] = {0, 0, 0};
    bool havePos = false;
    const Snapshot s = Sample(pos, havePos);
    MaybeAutoStart(s);

    const float dx = pos[0] - g_lastPos[0];
    const float dy = pos[1] - g_lastPos[1];
    const float dz = pos[2] - g_lastPos[2];
    const bool moved = havePos && std::sqrt(dx * dx + dy * dy + dz * dz) >= kPositionLogDistance;
    if (s == g_last && !moved) {
        return;
    }
    g_last = s;
    if (havePos) {
        std::copy(std::begin(pos), std::end(pos), std::begin(g_lastPos));
    }
    Logf("state: gameState {} menuActive {} currArea {} ped {:#010x} pedArea {} health {} armor {} money {} wanted {} "
         "pos {}",
         s.gameState, s.menuActive, s.currArea, s.ped, s.pedArea, s.health, s.armor,
         s.money == INT_MIN ? std::string("?") : std::to_string(s.money), s.wanted,
         havePos ? std::format("({:.1f}, {:.1f}, {:.1f})", pos[0], pos[1], pos[2]) : std::string("?"));
}

}  // namespace probe
