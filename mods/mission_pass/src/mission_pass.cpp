// Mission pass: passes the current story mission by key.
//
// Stage 1 (debug skip): almost every story mission still contains Rockstar's test branch
// "IF IS_PS2_KEYBOARD_KEY_PRESSED 83 ('S') -> GOTO <pass code>". In the PC release both key commands call a stub that
// always answers false. While the pass is armed, the mod answers true for key 83, and only for the mission thread
// (esi holds the CRunningScript at the call in both builds). The mission then runs its own pass code, cleanup and
// MISSION_HAS_FINISHED, exactly as in Rockstar's testing.
// Stage 2 (redirect): if the mission does not reach its check within 3 s (scripted scene, stage without the check),
// the mission thread is sent into its pass code the same way the engine unwinds a mission on wasted or busted: GOSUB
// stack back to the mission frame, instruction pointer at the pass entry. Entries come from a table verified against
// main.scm (tools/gen_mission_table.py); the mission is identified by a CRC of its first 64 bytes and the bytes at the
// entry are checked before anything is written.
#include "pass.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

// Read and written by the naked thunk (plain C names for the inline assembler).
extern "C" {
volatile unsigned char g_mpArmed = 0;  // answer "S pressed" for mission threads
volatile unsigned char g_mpFired = 0;  // a mission thread asked and got "pressed"
}

namespace mission_pass {
namespace {

constexpr MissionInfo kMissions[] = {
#include "mission_table.inc"
};

// Missions without pass code that end on their own once the player moves on. Finishing them = jumping to the RETURN
// that ends their body, but only once the thread waits in its last loop (all set-up done). Taken from the main.scm
// disassembly.
struct Finish {
    int index;
    int loopStart;  // the thread must be at or after this offset
    int target;     // RETURN of the body; the frame then runs the mission's cleanup chunk
    std::array<uint8_t, 8> targetBytes;
};
constexpr Finish kFinishes[] = {
    // INTRO "In the Beginning": waits at +9205 until CJ rides 30 m away or gets on the bike, then RETURNs at +9992;
    // the cleanup chunk (+9996) starts the next threads and puts up the Big Smoke marker.
    {2, 9205, 9992, {0x51, 0x00, 0x51, 0x00, 0x4F, 0x00, 0x01, 0xC3}},
};

const Finish* FinishFor(const MissionInfo* mission) {
    for (const auto& finish : kFinishes) {
        if (mission && finish.index == mission->index) {
            return &finish;
        }
    }
    return nullptr;
}

// CRunningScript fields (both builds).
constexpr size_t kNext = 0x00, kName = 0x08, kBaseIp = 0x10, kCurrentIp = 0x14, kStack = 0x18, kStackPointer = 0x38,
                 kWakeTime = 0xCC, kLogicalOp = 0xD0, kNotFlag = 0xD2, kWastedOrBusted = 0xD4, kSceneSkip = 0xD8,
                 kIsMission = 0xDC;
constexpr int kStatMissionsPassed = 147;

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
void(__cdecl* g_origPressed)() = nullptr;
void(__cdecl* g_origJustPressed)() = nullptr;

uint8_t* g_armedScript = nullptr;  // the mission thread the pass was armed for
const MissionInfo* g_armedMission = nullptr;
std::string g_armedTitle;
ULONGLONG g_armedAt = 0;
ULONGLONG g_confirmAt = 0;
int g_statBefore = 0;
bool g_redirected = false;
bool g_passing = false;  // armed (g_mpArmed covers only the debug skip answer)
ULONGLONG g_passTimeout = 30000;
ULONGLONG g_redirectedAt = 0;
bool g_stuckLogged = false;
PassPlan g_plan;
std::string g_lastRefusal;

// thiscall bool(int key, int, const char*), ret 0Ch, like the retail stub it replaces.
__declspec(naked) void __cdecl KeyThunk() {
    __asm {
        cmp dword ptr [esp + 4], 83
        jne not_pressed
        cmp byte ptr [g_mpArmed], 0
        je not_pressed
        test esi, esi
        jz not_pressed
        cmp byte ptr [esi + 0xDC], 1
        jne not_pressed
        mov byte ptr [g_mpFired], 1
        mov al, 1
        ret 0Ch
    not_pressed:
        xor al, al
        ret 0Ch
    }
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

template <class T>
T& Field(uint8_t* script, size_t offset) {
    return *reinterpret_cast<T*>(script + offset);
}

}  // namespace

uint8_t* MissionScript() {
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    int guard = 0;
    for (uint8_t* script = head ? *head : nullptr; script && guard < 512; script = Field<uint8_t*>(script, kNext)) {
        ++guard;
        if (Field<uint8_t>(script, kIsMission) == 1) {
            return script;
        }
    }
    return nullptr;
}

bool ScriptRunning(const char* name) {
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    int guard = 0;
    for (uint8_t* script = head ? *head : nullptr; script && guard < 512; script = Field<uint8_t*>(script, kNext)) {
        ++guard;
        if (_strnicmp(reinterpret_cast<const char*>(script + kName), name, 8) == 0) {
            return true;
        }
    }
    return false;
}

const MissionInfo* Identify(uint8_t* script) {
    const uint32_t crc = Crc32(Field<uint8_t*>(script, kBaseIp), 64);
    for (const auto& mission : kMissions) {
        if (mission.crc == crc) {
            return &mission;
        }
    }
    return nullptr;
}

const MissionInfo* ByIndex(int index) {
    for (const auto& mission : kMissions) {
        if (mission.index == index) {
            return &mission;
        }
    }
    return nullptr;
}

sacore::mod::Host& Host() {
    return *g_host;
}

sacore::sa::Game& Game() {
    return *g_game;
}

int32_t& GlobalInt(int n) {
    return *reinterpret_cast<int32_t*>(g_host->Symbol("CTheScripts::ScriptSpace") + 4 * n);
}

// A few buffers in turn: a thread reads its commands one step after it was started.
uint8_t g_runBuffers[4][1024];

void ScriptCounts(int& active, int& ours) {
    active = 0;
    ours = 0;
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    for (uint8_t* script = head ? *head : nullptr; script && active < 512; script = Field<uint8_t*>(script, kNext)) {
        ++active;
        const uint8_t* ip = Field<uint8_t*>(script, kCurrentIp);
        ours += ip >= &g_runBuffers[0][0] && ip < &g_runBuffers[0][0] + sizeof(g_runBuffers) ? 1 : 0;
    }
}

bool RunScript(const uint8_t* code, size_t size) {
    auto& buffers = g_runBuffers;
    static int next = 0;
    const auto start = g_host->GameFunction<void*(__cdecl*)(uint8_t*)>("CTheScripts::StartNewScript");
    if (!start || size > sizeof(buffers[0])) {
        return false;
    }
    uint8_t* buffer = buffers[next];
    next = (next + 1) % 4;
    std::memcpy(buffer, code, size);
    return start(buffer) != nullptr;
}

float& GlobalFloat(int n) {
    return *reinterpret_cast<float*>(g_host->Symbol("CTheScripts::ScriptSpace") + 4 * n);
}

namespace {

std::string TitleOf(uint8_t* script, const MissionInfo* mission) {
    if (mission && mission->title[0] != '\0') {
        return mission->title;
    }
    char name[9] = {};
    std::memcpy(name, script + kName, 8);
    return name[0] != '\0' ? std::string(name) : std::string("this mission");
}

std::string KeyName(int64_t vk) {
    return vk >= VK_F1 && vk <= VK_F24 ? "F" + std::to_string(vk - VK_F1 + 1) : "key " + std::to_string(vk);
}

}  // namespace

void Message(const std::string& text) {
    if (g_host->ConfigBool("show_messages", true)) {
        g_game->ShowHelp(text.c_str());
    }
    g_host->Info("{}", text);
}

namespace {

void Disarm() {
    g_mpArmed = 0;
    g_passing = false;
    g_armedScript = nullptr;
    g_armedMission = nullptr;
    g_plan = {};
    g_lastRefusal.clear();
}

}  // namespace

bool Passing() {
    return g_passing;
}

void Arm(uint8_t* script, const MissionInfo* mission, unsigned timeoutMs, bool preferPassCode, const PassPlan& plan) {
    g_passTimeout = timeoutMs;
    g_plan = plan;
    g_armedScript = script;
    g_armedMission = mission;
    g_armedTitle = TitleOf(script, mission);
    g_armedAt = GetTickCount64();
    g_statBefore = g_game->StatInt(kStatMissionsPassed);
    g_redirected = false;
    g_mpFired = 0;
    const bool passCode =
        preferPassCode && mission && (plan.mode == PassMode::Entry || mission->redirect || FinishFor(mission));
    g_mpArmed = plan.mode == PassMode::DebugSkip || (!passCode && g_host->ConfigBool("debug_skip", true)) ? 1 : 0;
    g_passing = true;
    g_host->Info("armed for mission {} '{}' (missions passed so far {})", mission ? mission->index : -1, g_armedTitle,
                 g_statBefore);
    Message("Passing " + g_armedTitle + "...");
}

namespace {

void PassKey() {
    if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9) {
        return;
    }
    if (g_passing) {
        Message("Already passing " + g_armedTitle + ".");
        return;
    }
    uint8_t* script = g_game->OnMission() ? MissionScript() : nullptr;
    if (!script) {
        Message("No mission is running.");
        return;
    }
    const MissionInfo* mission = Identify(script);
    if (g_host->ConfigBool("confirm", true)) {
        const ULONGLONG now = GetTickCount64();
        if (now - g_confirmAt > 3000) {
            g_confirmAt = now;
            Message("Press " + KeyName(g_host->ConfigInt("pass_key", VK_F6)) + " again to pass " +
                    TitleOf(script, mission) + ".");
            return;
        }
    }
    g_confirmAt = 0;
    Arm(script, mission, 30000);
}

// Why the redirect cannot run now (empty: it can).
std::string RedirectRefusal(uint8_t* script) {
    const MissionInfo* mission = g_armedMission;
    const Finish* finish = FinishFor(mission);
    const bool custom = g_plan.mode == PassMode::Entry;
    if (!mission || (!custom && !mission->redirect && !finish)) {
        return "no known pass code";
    }
    if (g_game->PlayerState() != 0) {
        return "player wasted or busted";
    }
    if (g_game->CutsceneRunning()) {
        return "cutscene running";
    }
    // A loaded cutscene keeps the player's controls off (CPad flag 0x80) until the mission plays and clears it;
    // jumping past it would leave CJ frozen.
    if (const auto* load = g_host->GameVariable<int>("CCutsceneMgr::ms_cutsceneLoadStatus"); load && *load != 0) {
        return "cutscene loaded";
    }
    uint8_t* base = Field<uint8_t*>(script, kBaseIp);
    const uint8_t* ip = Field<uint8_t*>(script, kCurrentIp);
    if (Field<uint16_t>(script, kStackPointer) < 1) {
        return "mission frame not on the stack";
    }
    if (ip < base || ip >= base + mission->size) {
        return "thread outside the mission code";
    }
    // m_apStack[0] returns into the frame: IF 0 / HAS_DEATHARREST_BEEN_EXECUTED, after the GOSUB of the body.
    const uint8_t* frame = Field<uint8_t*>(script, kStack);
    static constexpr uint8_t kFrame[] = {0xD6, 0x00, 0x04, 0x00, 0x12, 0x01};
    static constexpr uint8_t kGosub[] = {0x50, 0x00, 0x01};
    if (frame < base + 7 || frame + sizeof(kFrame) > base + mission->size ||
        std::memcmp(frame, kFrame, sizeof(kFrame)) != 0 || std::memcmp(frame - 7, kGosub, sizeof(kGosub)) != 0) {
        return "mission frame not found";
    }
    if (custom) {
        return g_plan.entry > 0 && g_plan.entry + 8 <= mission->size &&
                       std::memcmp(base + g_plan.entry, g_plan.bytes.data(), 8) == 0
                   ? std::string{}
                   : "entry differs from the expected script bytes";
    }
    if (finish) {
        if (ip < base + finish->loopStart || ip >= base + finish->target) {
            return "mission still setting up";
        }
        return std::memcmp(base + finish->target, finish->targetBytes.data(), 8) == 0 ? std::string{}
                                                                                      : "end of mission differs";
    }
    if (std::memcmp(base + mission->passEntry, mission->entryBytes.data(), 8) != 0) {
        return "pass code differs from the table";
    }
    static constexpr uint8_t kRegister[] = {0x18, 0x03, 0x09};
    if (std::memcmp(base + mission->register318, kRegister, 3) != 0 ||
        std::memcmp(base + mission->register318 + 3, mission->key, std::strlen(mission->key)) != 0) {
        return "REGISTER_MISSION_PASSED not found";
    }
    return {};
}

// The thread runs this before the pass code. The contact trigger fades the screen out and takes the camera before it
// launches a mission, and the mission body that would undo that is skipped, so the jump goes through the same script
// commands the missions use: DO_FADE 500 1 (fade in), RESTORE_CAMERA_JUMPCUT, SWITCH_WIDESCREEN 0, SET_PLAYER_CONTROL
// $PLAYER 1 (the trigger took the controls away), SET_AREA_VISIBLE <CJ's own area> when it differs (a mission that
// started a scene in an interior, such as House Party, leaves the visible area there otherwise), then GOTO the target
// (negative = relative to the mission block). The script engine reads commands from wherever the instruction pointer points.
// With PassPlan::moveFirst, SET_CHAR_AREA_VISIBLE and SET_CHAR_COORDINATES $3 x y -100 (ground height) come before
// the GOTO.
uint8_t g_jumpStub[80];

uint8_t* JumpStub(int target) {
    const uint8_t* ped = g_game->PlayerPed();
    const bool move = g_plan.moveFirst;
    const uint8_t area = move && g_plan.moveArea >= 0 ? static_cast<uint8_t>(g_plan.moveArea)
                         : ped                        ? ped[0x2F]  // CEntity::m_nAreaCode
                                                      : 0;
    const uint8_t prologue[] = {
        0x6A, 0x01, 0x05, 0xF4, 0x01, 0x04, 0x01,  // 016A DO_FADE 500 1
        0xEB, 0x02,                                // 02EB RESTORE_CAMERA_JUMPCUT
        0xA3, 0x02, 0x04, 0x00,                    // 02A3 SWITCH_WIDESCREEN 0
        0xB4, 0x01, 0x02, 0x08, 0x00, 0x04, 0x01,  // 01B4 SET_PLAYER_CONTROL $2 (player, global at +8) 1
    };
    size_t size = 0;
    auto put = [&size](const void* data, size_t n) {
        std::memcpy(g_jumpStub + size, data, n);
        size += n;
    };
    put(prologue, sizeof(prologue));
    // Only when the visible area differs: every SET_AREA_VISIBLE restarts the object brains around CJ (in a casino
    // some 50 script threads at once on top of the ~37 running; the game has 96 and crashes when none is free).
    const int* visible = g_host->GameVariable<int>("CGame::currArea");
    if (!visible || *visible != area) {
        const uint8_t setArea[] = {0xBB, 0x04, 0x04, area};  // 04BB SET_AREA_VISIBLE area
        put(setArea, sizeof(setArea));
    }
    if (move) {
        const uint8_t charArea[] = {0x60, 0x08, 0x02, 0x0C, 0x00, 0x04, area};  // 0860 SET_CHAR_AREA_VISIBLE $3 area
        put(charArea, sizeof(charArea));
        static constexpr uint8_t kSetCoords[] = {0xA1, 0x00, 0x02, 0x0C, 0x00};  // 00A1 SET_CHAR_COORDINATES $3
        const float point[3] = {g_plan.moveTo[0], g_plan.moveTo[1], -100.0f};   // -100: the game finds the ground
        put(kSetCoords, sizeof(kSetCoords));
        for (const float value : point) {
            const uint8_t type = 0x06;  // float
            put(&type, 1);
            put(&value, 4);
        }
    }
    if (g_plan.local >= 0) {
        const auto index = static_cast<uint16_t>(g_plan.local);
        const uint8_t setLocal[] = {0x06, 0x00, 0x03, static_cast<uint8_t>(index & 0xFF), static_cast<uint8_t>(index >> 8),
                                    0x04, static_cast<uint8_t>(g_plan.localValue)};  // 0006 SET_LVAR_INT local int8
        put(setLocal, sizeof(setLocal));
    }
    static constexpr uint8_t kGoto[] = {0x02, 0x00, 0x01};  // 0002 GOTO int32
    put(kGoto, sizeof(kGoto));
    const int32_t offset = -target;
    put(&offset, sizeof(offset));
    return g_jumpStub;
}

void Redirect(uint8_t* script) {
    const Finish* finish = g_plan.mode == PassMode::Entry ? nullptr : FinishFor(g_armedMission);
    const int target = g_plan.mode == PassMode::Entry ? g_plan.entry : finish ? finish->target : g_armedMission->passEntry;
    Field<uint16_t>(script, kStackPointer) = 1;  // keep m_apStack[0], the return into the mission frame
    Field<uint8_t*>(script, kCurrentIp) = JumpStub(target);
    Field<uint32_t>(script, kWakeTime) = 0;
    Field<int32_t>(script, kSceneSkip) = 0;
    Field<uint8_t>(script, kWastedOrBusted) = 0;
    Field<uint16_t>(script, kLogicalOp) = 0;
    Field<uint8_t>(script, kNotFlag) = 0;
    g_redirected = true;
    g_redirectedAt = GetTickCount64();
    g_stuckLogged = false;
    g_host->Info("redirected mission {} to its {} at +{}", g_armedMission->index,
                 g_plan.mode == PassMode::Entry ? "entry" : finish ? "end" : "pass code", target);
}

// Game thread, between script ticks (after CGame::Process).
void Tick() {
    if (!g_passing) {
        return;
    }
    uint8_t* script = MissionScript();
    // A new mission thread can reuse the finished one's CRunningScript (CAT launches the job right after CATALIN).
    if (!g_game->OnMission() || script != g_armedScript || (g_armedMission && Identify(script) != g_armedMission)) {
        const int passed = g_game->StatInt(kStatMissionsPassed);
        const char* how = g_redirected ? "pass code" : g_mpFired ? "debug skip" : "mission end";
        if (passed > g_statBefore) {
            g_host->Info("mission passed: '{}' via {} (missions passed {} -> {})", g_armedTitle, how, g_statBefore,
                         passed);
            Message("Mission passed: " + g_armedTitle + ".");
        } else {
            g_host->Info("mission '{}' ended without MISSION PASSED via {} (missions passed {})", g_armedTitle, how,
                         passed);
            if (FinishFor(g_armedMission)) {
                Message("Finished " + g_armedTitle + ".");
            }
        }
        Disarm();
        return;
    }
    const ULONGLONG elapsed = GetTickCount64() - g_armedAt;
    const ULONGLONG wait = g_mpArmed ? 3000 : 0;  // give the debug skip 3 s to answer
    if (g_plan.mode != PassMode::DebugSkip && !g_mpFired && !g_redirected && elapsed >= wait &&
        g_host->ConfigBool("allow_redirect", true)) {
        const std::string refusal = RedirectRefusal(script);
        if (refusal.empty()) {
            Redirect(script);
        } else if (refusal != g_lastRefusal) {
            g_lastRefusal = refusal;
            g_host->Info("pass code jump waits: {}", refusal);
        }
    }
    if (g_redirected && !g_stuckLogged && GetTickCount64() - g_redirectedAt > 5000) {
        // Normally the pass code and cleanup finish within a few ticks: record where the thread is.
        g_stuckLogged = true;
        uint8_t* base = Field<uint8_t*>(script, kBaseIp);
        g_host->Warn("still running 5 s after the jump: ip +{}, stack depth {}, wake {}, game state {}",
                     Field<uint8_t*>(script, kCurrentIp) - base, Field<uint16_t>(script, kStackPointer),
                     Field<uint32_t>(script, kWakeTime), g_host->GameState());
    }
    if (elapsed > g_passTimeout) {
        g_host->Warn("could not pass '{}' within {} s (debug skip {}, last reason: {})", g_armedTitle,
                     g_passTimeout / 1000,
                     !g_mpArmed ? "off" : g_mpFired ? "answered" : "not reached",
                     g_lastRefusal.empty() ? "-" : g_lastRefusal);
        Message("Could not pass " + g_armedTitle + " here. Try again later in the mission.");
        Disarm();
    }
}

void BindKeys() {
    g_host->ClearKeys();
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("pass_key", VK_F6)), &PassKey);
}

}  // namespace
}  // namespace mission_pass

using namespace mission_pass;

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("mission.pass") || !g_host->Has("game.mission.read")) {
        g_host->Warn("mission pass is not available on this build");
        return 0;
    }
    using StubFn = void __cdecl();
    const bool hooked =
        g_host->Hook<StubFn>("key_pressed", "ScriptDebugKeyPressed", "answer Rockstar's 'S' mission skip check",
                             g_host->GameFunction<StubFn*>("ScriptDebugKeyPressed"), &KeyThunk, &g_origPressed) &&
        g_host->Hook<StubFn>("key_just_pressed", "ScriptDebugKeyJustPressed",
                             "answer Rockstar's 'S' mission skip check",
                             g_host->GameFunction<StubFn*>("ScriptDebugKeyJustPressed"), &KeyThunk,
                             &g_origJustPressed);
    if (!hooked) {
        g_host->Warn("debug skip check not hooked, only the pass code jump is available");
    }
    BindKeys();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent& e) {
        BindKeys();
        if (e.text && std::string(e.text) == "run_to") {
            StoryConfigChanged();
        }
    });
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) {
        Tick();
        StoryTick();
    });
    g_host->On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent&) { Disarm(); });
    g_host->On(SACORE_EVENT_SAVE_COMPLETED, [](const SacoreEvent& e) { StorySaveCompleted(e.success != 0); });
    // Diagnostics and tests: log each mission start with its table index.
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) {
        static uint8_t* last = nullptr;
        static const MissionInfo* lastMission = nullptr;
        uint8_t* script = g_game->OnMission() ? MissionScript() : nullptr;
        const MissionInfo* mission = script ? Identify(script) : nullptr;
        if (script && (script != last || mission != lastMission)) {
            g_host->Info("mission started: {} '{}'", mission ? mission->index : -1, TitleOf(script, mission));
        }
        last = script;
        lastMission = mission;
    });
    StoryStart();
    g_host->Info("ready: pass key {}{}", KeyName(g_host->ConfigInt("pass_key", VK_F6)),
                 g_host->ConfigBool("confirm", true) ? " (press twice)" : "");
    return 0;
}
