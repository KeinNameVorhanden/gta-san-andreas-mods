// Shared declarations of the early probe (superseded by the core runtime).
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>

namespace probe {

// Per-build addresses as RVAs (offset from the runtime module base). See docs/ADDRESSES.md.
struct KnownBuild {
    const char* id;
    const char* sha256;
    uintptr_t gameStateRva;     // int gGameState
    uintptr_t menuManagerRva;   // CMenuManager FrontEndMenuManager
    uintptr_t playersRva;       // CPlayerInfo CWorld::Players[]
    uintptr_t currAreaRva;      // int CGame::currArea, 0 if not mapped
    uintptr_t genericSaveRva;   // bool CGenericGameStorage::GenericSave(int)
    const char* genericSavePrologue;
    size_t genericSavePrologueSize;
    uintptr_t genericLoadRva;   // bool CGenericGameStorage::GenericLoad(bool*)
    const char* genericLoadPrologue;
    size_t genericLoadPrologueSize;
};

void Log(std::string_view message);

template <class... Args>
void Logf(std::format_string<Args...> fmt, Args&&... args) {
    Log(std::format(fmt, std::forward<Args>(args)...));
}

uintptr_t ModuleBase();
std::span<const unsigned char> MainTextSection();

// Number of times the 32-bit value `address` appears in the main .text section (relocated in memory),
// used as a cheap signature that the code really references an address.
size_t CountCodeReferences(uintptr_t address);

// Reads memory with SEH protection; false if the address is not readable.
bool SafeRead(uintptr_t address, void* out, size_t size);

template <class T>
bool SafeRead(uintptr_t address, T& out) {
    return SafeRead(address, &out, sizeof(T));
}

// game_state.cpp: change-only logging of game state, called on the game thread.
void GameStateInit(const KnownBuild& build, bool autoStartNewGame);
void GameStateTick();

// events.cpp: save/load event hooks and the opt-in money write; MinHook must be initialized.
void EventsInit(const KnownBuild& build, int giveMoneyOnStart);
void EventsTick();

}  // namespace probe
