// Cutscene skip: skips cutscenes and the game's skippable scripted scenes with one key, or
// automatically. It answers the game's own "is the skip button pressed" question
// (CCutsceneMgr::IsCutsceneSkipButtonBeingPressed), so the game skips exactly as with its own skip button: the
// cutscene ends cleanly and the mission script continues from its skip point.
//
// Phone calls: the call script (main.scm, the same file on both
// builds) waits for each spoken line in a loop that ends the call when $1417 == 1 and the enter-vehicle button is
// pressed (main+193359: SET $1417 = 2, RETURN). The mod sends the waiting call thread to that branch, so the call
// ends at once exactly as with the game's own skip and the story effect of the call is kept ($1414 stays 1).
// Answering sets $1414 = 1 while the phone rings, as the call script does when the answer button is pressed.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
bool(__cdecl* g_origSkipPressed)() = nullptr;
ULONGLONG g_skipUntil = 0;     // a key press counts as "held" for a moment, the game polls once per frame
ULONGLONG g_sceneStart = 0;    // start of the current cutscene (for the log)
bool g_auto = false;
int g_skipped = 0;
std::string g_skipCalls = "key";
bool g_answerCalls = false;
bool g_callScriptChecked = false;
bool g_callScriptKnown = false;

// CRunningScript fields (both builds).
constexpr size_t kNext = 0x00, kCurrentIp = 0x14, kIsMission = 0xDC;
// main.scm offsets of the call script.
constexpr size_t kLineWaitResume = 193178;  // after the WAIT 0 of the loop that waits for a spoken line to finish
constexpr size_t kLineWaitSkip = 193390;    // SET_VAR_INT $1417 2, RETURN: the loop's skip-button branch
constexpr uint8_t kWaitBytes[] = {0x01, 0x00, 0x04, 0x00};                                // 193174: WAIT 0
constexpr uint8_t kSkipBytes[] = {0x04, 0x00, 0x02, 0x24, 0x16, 0x04, 0x02, 0x51, 0x00};  // SET $1417 2, RETURN
constexpr int kCallRunning = 15, kRings = 1409, kAnswered = 1414, kConversation = 1417;

template <typename T>
T& Field(uint8_t* script, size_t offset) {
    return *reinterpret_cast<T*>(script + offset);
}

uint8_t* ScriptSpace() {
    return reinterpret_cast<uint8_t*>(g_host->Symbol("CTheScripts::ScriptSpace"));
}

int32_t& GlobalInt(int n) {
    return *reinterpret_cast<int32_t*>(ScriptSpace() + 4 * n);
}

// A modified main.scm moves the call script: only act on the bytes this mod was written for.
bool CallScriptKnown() {
    if (!g_callScriptChecked) {
        g_callScriptChecked = true;
        const uint8_t* space = ScriptSpace();
        g_callScriptKnown = std::memcmp(space + kLineWaitResume - sizeof(kWaitBytes), kWaitBytes, sizeof(kWaitBytes)) == 0 &&
                            std::memcmp(space + kLineWaitSkip, kSkipBytes, sizeof(kSkipBytes)) == 0;
        if (!g_callScriptKnown) {
            g_host->Warn("main.scm differs from the original game: phone calls are not answered or skipped");
        }
    }
    return g_callScriptKnown;
}

bool MissionThreadRunning() {
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    int guard = 0;
    for (uint8_t* script = head ? *head : nullptr; script && guard < 512; script = Field<uint8_t*>(script, kNext)) {
        ++guard;
        if (Field<uint8_t>(script, kIsMission) == 1) {
            return true;
        }
    }
    return false;
}

// The call thread waiting for its current line to finish, or null.
uint8_t* CallThreadInLine() {
    const uint8_t* resume = ScriptSpace() + kLineWaitResume;
    auto* const* head = g_host->GameVariable<uint8_t*>("CTheScripts::pActiveScripts");
    int guard = 0;
    for (uint8_t* script = head ? *head : nullptr; script && guard < 512; script = Field<uint8_t*>(script, kNext)) {
        ++guard;
        if (Field<const uint8_t*>(script, kCurrentIp) == resume) {
            return script;
        }
    }
    return nullptr;
}

void PhoneTick() {
    if (!g_host->ConfigBool("enabled", true) || !g_host->Has("phone.call_skip") || g_host->GameState() != 9 ||
        GlobalInt(kCallRunning) != 1 || !CallScriptKnown()) {
        return;
    }
    // Ringing: answer like the answer button (the script also wants CAN_PLAYER_START_MISSION, so no mission and
    // the controls free).
    if (g_answerCalls && GlobalInt(kRings) > 0 && GlobalInt(kAnswered) == 0 && GlobalInt(kConversation) == 0 &&
        !MissionThreadRunning() && !g_game->ControlsDisabled() && g_game->PlayerState() == 0) {
        GlobalInt(kAnswered) = 1;
        g_host->Info("phone call answered");
    }
    if (g_skipCalls == "off" || GlobalInt(kConversation) != 1) {
        return;
    }
    if (g_skipCalls != "auto" && GetTickCount64() >= g_skipUntil) {
        return;
    }
    if (uint8_t* thread = CallThreadInLine()) {
        Field<uint8_t*>(thread, kCurrentIp) = ScriptSpace() + kLineWaitSkip;
        g_skipUntil = 0;
        g_host->Info("phone call skipped");
    }
}

bool __cdecl HookedSkipPressed() {
    const bool cutscene = g_game->CutsceneRunning();
    if (g_auto || GetTickCount64() < g_skipUntil) {
        if (cutscene) {
            ++g_skipped;
        }
        return true;
    }
    return g_origSkipPressed();
}

void ReadSettings() {
    const bool enabled = g_host->ConfigBool("enabled", true);
    g_auto = enabled && g_host->ConfigBool("auto_skip", false);
    g_skipCalls = g_host->ConfigString("skip_calls");
    g_answerCalls = g_host->ConfigBool("answer_calls", false);
    g_host->ClearKeys();
    if (enabled) {
        g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("skip_key", VK_RETURN)),
                      [] { g_skipUntil = GetTickCount64() + 400; });
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("cutscene.skip")) {
        g_host->Warn("cutscene skip is not available on this build");
        return 0;
    }
    using SkipFn = bool __cdecl();
    if (!g_host->Hook<SkipFn>("skip_button", "CCutsceneMgr::IsCutsceneSkipButtonBeingPressed",
                              "skip cutscenes by key or automatically",
                              g_host->GameFunction<SkipFn*>("CCutsceneMgr::IsCutsceneSkipButtonBeingPressed"),
                              &HookedSkipPressed, &g_origSkipPressed)) {
        g_host->Error("could not hook the skip button check");
        return 1;
    }
    ReadSettings();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { ReadSettings(); });
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { PhoneTick(); });
    // Log each cutscene with its length and whether it was skipped (diagnostics and tests).
    g_host->On(SACORE_EVENT_FRAME, [](const SacoreEvent&) {
        static bool running = false;
        const bool now = g_game->CutsceneRunning();
        if (now && !running) {
            g_sceneStart = GetTickCount64();
            g_skipped = 0;
        } else if (!now && running) {
            g_host->Info("cutscene ended after {:.1f} s{}", (GetTickCount64() - g_sceneStart) / 1000.0,
                         g_skipped > 0 ? " (skipped)" : "");
        }
        running = now;
    });
    if (!g_host->Has("phone.call_skip")) {
        g_host->Info("phone calls are not supported on this build");
    }
    g_host->Info("ready: skip key {}, automatic {}, calls: skip {}, answer {}", g_host->ConfigInt("skip_key", VK_RETURN),
                 g_auto, g_skipCalls, g_answerCalls);
    return 0;
}
