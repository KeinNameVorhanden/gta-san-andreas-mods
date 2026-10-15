// Narrow event hooks on the save system (SaveStarted / SaveCompleted, LoadStarted / LoadCompleted)
// and one safe, opt-in state write (money). Patches H6, H7 and W1 in docs/ADDRESSES.md.

#include "probe.h"

#include <MinHook.h>

#include <climits>
#include <cstring>
#include <string>

namespace probe {
namespace {

constexpr uintptr_t kPlayerInfoMoneyOffset = 0xB8;
constexpr int kGameStatePlayingGame = 9;

using GenericSaveFn = bool(__cdecl*)(int);
using GenericLoadFn = bool(__cdecl*)(bool*);

GenericSaveFn g_origSave = nullptr;
GenericLoadFn g_origLoad = nullptr;
uintptr_t g_players = 0;
uintptr_t g_gameState = 0;
int g_giveMoney = 0;
bool g_moneyGiven = false;

int ReadMoney() {
    int money = INT_MIN;
    return g_players && SafeRead(g_players + kPlayerInfoMoneyOffset, money) ? money : INT_MIN;
}

bool __cdecl HookedGenericSave(int argument) {
    const ULONGLONG start = GetTickCount64();
    Logf("event SaveStarted (money {})", ReadMoney());
    const bool ok = g_origSave(argument);
    Logf("event SaveCompleted result {} after {} ms", ok, GetTickCount64() - start);
    return ok;
}

bool __cdecl HookedGenericLoad(bool* out) {
    const ULONGLONG start = GetTickCount64();
    Log("event LoadStarted");
    const bool ok = g_origLoad(out);
    Logf("event LoadCompleted result {} after {} ms", ok, GetTickCount64() - start);
    return ok;
}

std::string HexBytes(const unsigned char* bytes, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        text += std::format("{}{:02X}", i ? " " : "", bytes[i]);
    }
    return text;
}

// Hooks `rva` after checking its first bytes against `prologue`. A target that already starts with a JMP
// (hooked by another plugin such as SilentPatch) is chained by MinHook and logged.
bool HookChecked(uintptr_t rva, const char* prologue, size_t prologueSize, void* detour, void** original,
                 const char* name) {
    if (rva == 0) {
        Logf("hook {}: not mapped for this build", name);
        return false;
    }
    auto* target = reinterpret_cast<unsigned char*>(ModuleBase() + rva);
    unsigned char actual[16] = {};
    if (!SafeRead(reinterpret_cast<uintptr_t>(target), actual, prologueSize)) {
        Logf("hook {}: target {:#010x} unreadable, DISABLED", name, reinterpret_cast<uintptr_t>(target));
        return false;
    }
    const bool alreadyHooked = actual[0] == 0xE9;
    if (!alreadyHooked && std::memcmp(actual, prologue, prologueSize) != 0) {
        Logf("hook {}: signature mismatch at {:#010x}: expected {}, found {}, DISABLED", name,
             reinterpret_cast<uintptr_t>(target),
             HexBytes(reinterpret_cast<const unsigned char*>(prologue), prologueSize), HexBytes(actual, prologueSize));
        return false;
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    Logf("hook {} at {:#010x}{}: {}", name, reinterpret_cast<uintptr_t>(target),
         alreadyHooked ? " (already hooked by another module, chained)" : "", MH_StatusToString(status));
    return status == MH_OK;
}

}  // namespace

void EventsInit(const KnownBuild& build, int giveMoneyOnStart) {
    g_players = ModuleBase() + build.playersRva;
    g_gameState = ModuleBase() + build.gameStateRva;
    g_giveMoney = giveMoneyOnStart;
    HookChecked(build.genericSaveRva, build.genericSavePrologue, build.genericSavePrologueSize,
                reinterpret_cast<void*>(&HookedGenericSave), reinterpret_cast<void**>(&g_origSave),
                "CGenericGameStorage::GenericSave");
    HookChecked(build.genericLoadRva, build.genericLoadPrologue, build.genericLoadPrologueSize,
                reinterpret_cast<void*>(&HookedGenericLoad), reinterpret_cast<void**>(&g_origLoad),
                "CGenericGameStorage::GenericLoad");
    Logf("W1 give money on start: {}", g_giveMoney ? std::to_string(g_giveMoney) : std::string("off"));
}

void EventsTick() {
    if (g_giveMoney == 0 || g_moneyGiven) {
        return;
    }
    int state = 0;
    uintptr_t ped = 0;
    if (!SafeRead(g_gameState, state) || state != kGameStatePlayingGame || !SafeRead(g_players, ped) || !ped) {
        return;
    }
    const int before = ReadMoney();
    if (before == INT_MIN) {
        return;
    }
    const long long target = static_cast<long long>(before) + g_giveMoney;
    const int after = static_cast<int>(target > 999'999'999 ? 999'999'999 : (target < 0 ? 0 : target));
    *reinterpret_cast<volatile int*>(g_players + kPlayerInfoMoneyOffset) = after;
    g_moneyGiven = true;
    Logf("W1 money {} -> {} (read back {})", before, after, ReadMoney());
}

}  // namespace probe
