// Shared between the pass key (mission_pass.cpp) and the story runner (story_runner.cpp). Game thread only.
#pragma once

#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace mission_pass {

struct MissionInfo {
    int index;
    uint32_t crc;
    int size;
    int passEntry;  // -1: no pass code
    std::array<uint8_t, 8> entryBytes;
    int register318;
    const char* key;
    const char* title;
    bool redirect;  // stage 2 allowed
};

sacore::mod::Host& Host();
sacore::sa::Game& Game();

// The running mission thread (m_bIsMission), or nullptr.
uint8_t* MissionScript();
// Table entry of a mission thread (CRC of its first 64 bytes), or nullptr for an unknown main.scm.
const MissionInfo* Identify(uint8_t* script);
const MissionInfo* ByIndex(int index);

// How a mission thread is passed (the story runner picks it per mission).
enum class PassMode {
    Code,   // the table's pass code, or the debug skip for missions without one
    Entry,  // a researched entry in the mission (checked by its 8 bytes), for missions without REGISTER_MISSION_PASSED
    DebugSkip,  // only Rockstar's 'S' branch, never the jump: races (CJ is put on each checkpoint, so the race is won
                // for real) and missions whose pass code needs their own set-up (Air Raid)
};
struct PassPlan {
    PassMode mode = PassMode::Code;
    int entry = -1;
    std::array<uint8_t, 8> bytes{};
    // Put CJ (and his vehicle) here first, on the ground, in interior moveArea (-1: his own). For triggers that
    // would launch the same mission again in the step it ends, because CJ is still in the marker.
    bool moveFirst = false;
    float moveTo[2] = {};
    int moveArea = -1;
    // A mission local set first (SET_LVAR_INT in the mission's own thread), for pass code that checks the result.
    int local = -1;
    int localValue = 0;
};

// Pass the given mission thread now, without the confirmation press. timeoutMs: give up after this long.
// preferPassCode: jump into the pass code at once (the story runner: CJ stays on foot at the marker), the debug skip
// only for missions without a known pass code.
void Arm(uint8_t* script, const MissionInfo* mission, unsigned timeoutMs, bool preferPassCode = false,
         const PassPlan& plan = {});
bool Passing();
// A script thread with this name (SCRIPT_NAME) is running.
bool ScriptRunning(const char* name);
// Running script threads, and how many of them run RunScript code (diagnostics: the game has 96).
void ScriptCounts(int& active, int& ours);
void Message(const std::string& text);

// Runs script commands (main.scm byte code, ending with TERMINATE_THIS_SCRIPT, at most 1024 bytes) as a new script
// thread through CTheScripts::StartNewScript; it runs in the next script step. False when the build lacks the function.
bool RunScript(const uint8_t* code, size_t size);

// Main script global $n (ScriptSpace + 4 * n).
int32_t& GlobalInt(int n);
float& GlobalFloat(int n);

// Story runner (story_runner.cpp).
void StoryStart();
void StoryConfigChanged();
void StoryTick();
void StorySaveCompleted(bool success);
// "off" plus the titles the runner can stop before, '|'-separated (for the log).
std::string StoryTargets();

}  // namespace mission_pass
