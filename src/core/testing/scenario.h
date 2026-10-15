// Runtime test scenarios (TOML) executed inside the game by the harness. Parsing is pure and unit tested.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

enum class StepAction {
    WaitState,           // value: game state; timeout
    WaitSeconds,         // value: seconds
    ExpectCapability,    // id, state ("supported", "experimental", "unsupported", "not investigated")
    ExpectPlayer,        // field: health|armour|money|wanted|area|cutscene (0/1)|mission (0/1)|
                         //        menu (0/1: pause menu open or requested); min; max
    WaitPlayer,          // like expect_player but waits up to timeout
    SetMoney,            // value
    SimulateFocusLoss,   // sends deactivate messages to the game window
    SimulateFocusReturn, // sends activate messages (WM_SETFOCUS among them) to the game window
    SimulateMenuKey,     // posts the keyboard menu request F10 or a bare Alt produce (WM_SYSCOMMAND SC_KEYMENU)
    SetSetting,          // name (mod id), id (key), contains (value): a live setting as in the F11 console
    SetWaypoint,         // x, y: a map waypoint as the pause-menu map sets it (coordinate blip + menu handle)
    SetHealth,           // value: player health (0 = wasted)
    ImportSave,          // name: save name (slot3), id: file in sacore\import
    HoldKey,             // value: Windows key code held for `seconds` (the game sees it pressed the whole time)
    MarkPosition,        // remembers the player position for expect_distance
    ExpectDistance,      // min, max: 2D distance (m) from the marked position; the value is kept for expect_ratio
    ExpectRatio,         // min, max: ratio of the last two expect_distance values (second / first)
    Teleport,            // x, y, z: puts the player there (SET_CHAR_COORDINATES, run in the next game tick)
    PressKey,            // value: Windows key code, sent as key down + key up to the game window
    LoadSave,            // name: load a save the way the menu does (slot3, quick1, auto2)
    StartNewGame,        // "Start new game" from the pause menu while playing (menu routine, message loop)
    ExpectFramesAdvance, // value: seconds to watch; min (frames)
    ExpectLog,           // contains: text that must appear in this session's log; fail_contains: fails at once
    ExpectWindowCaption, // windowed mode active: the game window has a caption
    ExpectPatch,         // id, state ("installed", "disabled", "failed", "removed")
    SaveSlot,            // value: slot 1..8, saved through the game's own routine
    ExpectSaveValid,     // value: slot 1..8; file exists in the active User Files folder and passes checks
    CrashNow,            // deliberate access violation (crash scenarios)
    Quit,                // close the game window
};

struct ScenarioStep {
    StepAction action;
    double value = 0;
    double timeout = 30;
    double min = 0;
    double max = 0;
    double x = 0, y = 0, z = 0;  // teleport
    double seconds = 0;          // hold_key
    std::string text;  // id / field / contains
    std::string state;
    std::string mod;      // set_setting: mod id
    std::string settingValue;  // set_setting: value text // capability state
    std::string failText;      // expect_log: fail_contains
};

struct Scenario {
    std::string name;
    double timeoutSeconds = 180;
    bool expectCrash = false;
    std::vector<ScenarioStep> steps;
};

struct ScenarioParseResult {
    std::optional<Scenario> scenario;
    std::vector<std::string> errors;
};

ScenarioParseResult ParseScenario(std::string_view tomlText);
std::string_view ToString(StepAction action);

}  // namespace sacore
